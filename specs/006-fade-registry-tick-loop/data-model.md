# Data Model: Phase 4 — Fade Registry, Tick Loop & OSC Sender

**Branch**: `006-fade-registry-tick-loop` | **Date**: 2026-04-23

## Entity Overview

```
FadeCommand (Phase 3)
    │ parsed + enqueued by NngBusClient recv thread
    ▼
LockFreeQueue<FadeCommand, 64> (Phase 3)
    │ drained by tick thread (via GradientEngine::onTick)
    ▼
FadeRegistry::addFade / cancelFade / cancelAll
    │ materialises / removes
    ▼
ActiveFade  ──── (curve)   ──→ gme::gradient::Curve (Phase 1)
            ──── (target)  ──→ lo_address (liblo handle)
            │
            │ tick() iterates
            ▼
OscSender::sendFloat(target, path, value)  ──→ UDP OSC to AudioPlayer / VideoComposer
            │
            │ on complete / error
            ▼
StatusWorkerQueue<StatusEmitRequest, 64>
    │ consumed by NngBusClient status worker thread
    ▼
NngBusClient::sendStatus  ──→ NNG bus → Controller
```

---

## Entities

### `ActiveFade` (`src/engine/ActiveFade.h`)

One registered, currently-running fade. All fields are set at `addFade` time and read (not written) during `tick()` evaluation except `last_sent_value` and `completed`.

| Field | Type | Set at | Description |
|-------|------|--------|-------------|
| `fade_id` | `std::string` | addFade | Controller-assigned unique identifier. |
| `curve` | `std::unique_ptr<gme::gradient::Curve>` | addFade | Pre-resampled curve (ResampledCurve LUT, 256 samples). Built via `CurveFactory`. |
| `osc_target` | `lo_address` | addFade | Pre-built liblo address struct. One `lo_address_new(host, port)` per fade registration. |
| `osc_path` | `std::string` | addFade | OSC destination path, e.g. `/volmaster`. |
| `osc_host` | `std::string` | addFade | Stored for supersede-key computation and `lo_address` reconstruction if needed. |
| `osc_port` | `int` | addFade | UDP port of the target player. |
| `start_value` | `float` | addFade | Gain at t=0. Range [0.0, 1.0]. |
| `end_value` | `float` | addFade | Gain at t=1. Range [0.0, 1.0]. |
| `start_mtc_ms` | `long` | addFade | Absolute MTC start time in ms. The `-1` sentinel is resolved to `MtcTickSource::getMtcMs()` at addFade time. |
| `duration_ms` | `float` | addFade | Total fade duration in ms. If 0: fade completes immediately on first tick. |
| `last_sent_value` | `float` | tick() | Most recent value sent via OSC. Updated every tick. Used for cancel-with-hold and crash recovery. Initialised to `start_value`. |
| `completed` | `bool` | tick() | Set `true` when `t >= 1.0`. After `tick()` returns, the engine removes completed fades. |
| `consecutive_osc_failures` | `int` | tick() | Count of consecutive `lo_send` errors. Reset to 0 on success. Fade removed when this reaches `kOscFailureThreshold`. |
| `crossfade_partner` | `ActiveFade*` | addFade | Always `nullptr` in Phase 4. Reserved for Phase 7 crossfade pairing. |

**Lifecycle**:
```
REGISTERED (addFade) → TICKING (tick, each MTC QF) → COMPLETED (t>=1.0) → REMOVED
                    ↘ CANCELLED (cancelFade/cancelAll)                   ↗ REMOVED
                    ↘ ERROR (osc_send_failed, unknown_curve_type)        ↗ REMOVED
                    ↘ SUPERSEDED (new fade on same OSC path)             ↗ REMOVED
```

**Invariants**:
- `start_mtc_ms` is always a concrete non-negative value (never `-1`) after `addFade`.
- `osc_target` is non-null if and only if `ActiveFade` is in the registry.
- `curve` is non-null if and only if `ActiveFade` is in the registry.

**Destructor responsibility**: `FadeRegistry` calls `lo_address_free(osc_target)` when removing an `ActiveFade` from the map. The `unique_ptr<Curve>` destructs naturally.

---

### `FadeRegistry` (`src/engine/FadeRegistry.h/.cpp`)

Owns all active fades. Single-threaded access model: all methods are called exclusively from the MTC tick thread context (or fallback drain thread, serialised via `drain_in_progress_` in NngBusClient).

**Internal state**:

| Field | Type | Description |
|-------|------|-------------|
| `fades_` | `std::unordered_map<std::string, std::unique_ptr<ActiveFade>>` | Primary index: fade_id → ActiveFade. Pointer-stable after insert. |
| `osc_index_` | `std::unordered_map<std::string, std::string>` | Secondary index: `"host:port:path"` → fade_id. Used only in addFade/cancelFade for supersede detection (FR-014). Never accessed during tick(). |
| `mtcSource_` | `const gme::time::MtcTickSource&` | Reference for resolving `start_mtc_ms = -1`. |
| `statusQueue_` | `LockFreeQueue<StatusEmitRequest, 64>&` | Reference to NngBusClient's outbound status queue. Used by tick thread context to push status without blocking. |
| `statusDirect_` | `std::function<void(StatusKind, const std::string&, const std::string&)>` | Direct send callback used by the fallback drain thread context (calls `NngBusClient::sendStatus` directly). |

**Key operations**:

| Method | Called from | Description |
|--------|------------|-------------|
| `addFade(FadeCommand)` | Tick or drain thread | Build curve, resolve start_mtc_ms, pre-build lo_address, detect+remove superseded fade, insert into both indexes. |
| `cancelFade(fade_id, snap_to_end)` | Tick or drain thread | Remove from both indexes; if snap_to_end, send one final OSC at end_value. |
| `cancelAll()` | Tick or drain thread | Remove all from both indexes; no final OSC values (FR-009). |
| `tick(mtc_ms)` | Tick thread only | Evaluate all fades: compute t, value, send OSC, update last_sent_value, mark completed, count OSC failures. Returns list of fade_ids to remove (completed + errored). |

---

### `OscSender` (`src/osc/OscSender.h/.cpp`)

Free-function wrapper over liblo. No class instantiation required.

**API** (see [contracts/osc-sender-api.md](contracts/osc-sender-api.md)):

```cpp
namespace gme::osc {
    int sendFloat(lo_address target, const char* path, float value) noexcept;
}
```

**Dependencies**: liblo (`lo_address`, `lo_send`). CMakeLists.txt must link `gradient_motion` against liblo.

---

### `GradientEngine` (`src/engine/GradientEngine.h/.cpp`)

Top-level orchestrator. Owns `MtcTickSource`, `NngBusClient`, `LockFreeQueue`, `FadeRegistry`. Wires the tick callback and manages daemon lifetime.

| Field | Type | Description |
|-------|------|-------------|
| `tickSource_` | `gme::time::MtcTickSource` | Owned. Registered callback fires `onTick(mtc_ms)`. |
| `queue_` | `gme::signal::LockFreeQueue<FadeCommand, 64>` | Owned. Shared between NngBusClient (producer) and tick callback (consumer). |
| `statusQueue_` | `LockFreeQueue<StatusEmitRequest, 64>` | Owned. Shared between tick callback (producer) and NngBusClient status worker (consumer). |
| `nngClient_` | `gme::daemon::comms::NngBusClient` | Owned. Wired with queue_ and statusQueue_. |
| `registry_` | `gme::engine::FadeRegistry` | Owned. Wired with tickSource_, statusQueue_. |

---

### `StatusEmitRequest` (inline in `NngBusClient.h` or `FadeRegistry.h`)

Tuple pushed by the tick thread onto the status worker queue.

| Field | Type | Description |
|-------|------|-------------|
| `kind` | `StatusKind` | `FadeComplete` or `FadeError`. |
| `fade_id` | `std::string` | Fade identifier (from `ActiveFade::fade_id`). |
| `reason` | `std::string` | Error reason string. Empty for `FadeComplete`. |

**Note on allocation**: `std::string` copies in the status push are not in the steady-state per-frame tick path — they occur only on fade completion or error events (non-recurring). Acceptable per Principle IV (steady-state evaluation frames have no allocation; completion/error is an exceptional exit from the fade lifecycle).

---

## Relationships

```
GradientEngine
├── owns MtcTickSource              (1:1)
├── owns LockFreeQueue<FadeCommand> (1:1; shared producer NngBusClient, consumer tick)
├── owns LockFreeQueue<StatusReq>   (1:1; shared producer tick, consumer NngBusClient worker)
├── owns NngBusClient               (1:1)
└── owns FadeRegistry               (1:1)
    ├── holds 0..50 ActiveFade      (0..N)
    │   ├── owns unique_ptr<Curve>  (1:1 per fade)
    │   └── owns lo_address         (1:1 per fade)
    └── refs MtcTickSource          (borrowed, for start_mtc_ms resolution)
```

---

## State Transitions (FadeRegistry)

```
[Empty Registry]
       │ addFade(START_FADE)
       ▼
[fade_id in fades_ + osc_index_]
       │ tick(mtc_ms), t < 1.0
       ▼ (stays, last_sent_value updated)
       │ tick(mtc_ms), t >= 1.0
       ├─────────────────────────────────────→ push(FadeComplete) → [Removed]
       │ tick(mtc_ms), consecutive_osc_failures >= 5
       ├─────────────────────────────────────→ push(FadeError:"osc_send_failed") → [Removed]
       │ addFade(same osc_key, different fade_id)
       ├─────────────────────────────────────→ push(FadeError:"superseded") → [Removed, new fade added]
       │ cancelFade(snap_to_end=true)
       ├─────────────────────────────────────→ send end_value OSC → [Removed]
       │ cancelFade(snap_to_end=false)
       ├─────────────────────────────────────→ [Removed, no OSC]
       │ cancelAll()
       └─────────────────────────────────────→ [All Removed, no OSC]
```
