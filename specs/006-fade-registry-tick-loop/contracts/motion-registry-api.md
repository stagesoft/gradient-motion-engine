# Contract: `gme::motion::MotionRegistry` + `IMotion`

**Files**: `src/motion/MotionRegistry.h/.cpp`, `src/motion/IMotion.h`, `src/motion/EvalResult.h`  
**Namespace**: `gme::motion`  
**Thread model**: All public methods must be called from a single thread at a time (MTC tick thread or fallback drain thread, serialised externally via `NngBusClient::drain_in_progress_`). No internal mutex required.

---

## `IMotion` abstract interface

Every motion type subclasses `IMotion`. The registry interacts with motions exclusively through this interface (Constitution Principle VII).

### Shared lifecycle fields (public, set at construction)

| Field | Type | Description |
|-------|------|-------------|
| `motion_id` | `std::string` | Registry-unique identifier (maps to wire field `fade_id`). |
| `osc_key` | `std::string` | Composite supersede key: `"host:port:path"`. |
| `start_mtc_ms` | `long` | Absolute MTC start time in ms. Never -1 in a live entry. |
| `duration_ms` | `float` | Fade duration in ms. 0 → completes on first tick. |
| `completed` | `bool` | Set by registry when `evalAndSend` returns `completed=true`. |
| `consecutive_osc_failures` | `int` | Updated by registry on each tick; reset to 0 on success. |

### Pure-virtual methods

#### `EvalResult evalAndSend(long mtc_ms)`
Called once per active motion per tick from `MotionRegistry::tick`. Must not block, allocate, or throw. Transport failure is reported via `EvalResult::failed`, never by throwing.

#### `void sendSnapToEnd()`
Send one final transport message at the motion's terminal state. Called only by `cancelMotion(snap_to_end=true)`.

#### `void inheritFrom(const IMotion& prior)`
Adopt state from the motion being superseded on the same `osc_key`. Called by `addMotion` during supersede. On type mismatch the call may be a no-op.

---

## `EvalResult`

```cpp
struct EvalResult {
    bool        completed      = false;  // t reached 1.0 this tick
    bool        failed         = false;  // transport send failed this tick
    const char* failure_reason = nullptr; // static-storage; non-null only if failed
};
```

---

## `MotionRegistry::addMotion(std::unique_ptr<IMotion> m)`

**Ordered checks** (each runs before the next):

1. **Duplicate `motion_id` guard**: if `motions_` already contains `m->motion_id`, emit `MotionError:"duplicate_motion_id"` for the incoming id, drop the incoming motion (destructor frees transport handle + curve), return. The existing motion is untouched.
2. **`osc_key` supersede**: look up `m->osc_key` in `osc_index_`; on hit, emit `MotionError:"superseded"` for the old `motion_id`, call `m->inheritFrom(*old_motion)`, erase the old motion from both maps.
3. **Insert**: add `m` to `motions_[motion_id]` and `osc_index_[osc_key]`.

**Returns**: void. Errors signalled via status queue (tick context) or direct callback (drain context).  
**Throws**: Never.

---

## `MotionRegistry::cancelMotion(const std::string& motion_id, bool snap_to_end)`

1. Look up `motion_id` in `motions_`. If not found: log warning, return.
2. If `snap_to_end == true`: call `motion->sendSnapToEnd()`.
3. Remove from `motions_` and `osc_index_`.

**Throws**: Never.

---

## `MotionRegistry::cancelAll()`

Clears both `motions_` and `osc_index_`. Motions free their own transport handles via destructor. Does NOT call `sendSnapToEnd` on any motion.

**Throws**: Never.

---

## `MotionRegistry::tick(long mtc_ms)`

**Preconditions**: Called exclusively from the MTC tick thread after draining the command queue.

For each `IMotion m` in `motions_`:
1. `EvalResult r = m.evalAndSend(mtc_ms)`.
2. If `r.failed`: `++m.consecutive_osc_failures`. If `>= kOscFailureThreshold`: push `MotionError:"osc_send_failed"`, mark for removal. Continue.
   Else: `m.consecutive_osc_failures = 0`.
3. If `r.completed`: `m.completed = true`, push `MotionComplete`, mark for removal.

After iterating: remove all marked motions from both maps (destructors run).

**Throws**: Never.

---

## Status emission model

| Context | Method | Condition |
|---------|--------|-----------|
| Tick thread (`tickThreadContext_ = true`) | `statusQueue_.push(req)` | Completion, osc_send_failed (from tick) |
| Drain thread (`tickThreadContext_ = false`) | `statusDirect_(kind, id, reason)` | Supersede, duplicate_motion_id, construction errors (from MotionFactory) |

`setTickThreadContext(bool v)` is called by `GradientEngine::onTick` before/after each tick cycle.

---

## Constants

| Name | Value | Description |
|------|-------|-------------|
| `kOscFailureThreshold` | `5` | Consecutive transport failures before a motion is declared dead. At 200 Hz = 25 ms of silence. |
| `kStatusQueueCapacity` | `64` | SPSC status queue capacity (matches inbound command queue). |

---

## Supersede rule (single-path invariant)

At most one active motion per `osc_key` at any time. A new motion with the same `osc_key` supersedes the existing one via the **same** `addMotion` ordered check (#2). Multi-path outputs are represented by multiple simultaneous motions with distinct keys. Multi-dimensional payloads are a single motion whose `evalAndSend` emits one OSC message with N arguments (see `VectorMotion<N>` below).

The duplicate-`motion_id` check (#1) runs **before** the supersede check (#2). A "retarget" (same id, different path) is rejected; use `cancel_motion` + `start_motion` instead.

---

## Future extension — `VectorMotion<N>`

An N-dimensional payload motion can be added without changing the registry or tick loop:

1. Subclass `IMotion` as `VectorMotion<N>` (N ∈ {2, 3, 4}).
2. Own `std::array<float, N>` for `start_values`, `end_values`, `last_sent_values`.
3. `evalAndSend` computes N values and emits a single OSC message with N float arguments via `lo_send(target, path, "fff...", v0, v1, ...)`.
4. `osc_key` remains a single `"host:port:path"` composite — supersede keeps its single-key invariant. Multiple output paths → multiple `VectorMotion<N>` instances.
5. `MotionFactory::fromCommand` adds a `START_VECTOR_MOTION` case; no other infrastructure changes.

For scalar fades, see [`fade-motion-api.md`](fade-motion-api.md).
