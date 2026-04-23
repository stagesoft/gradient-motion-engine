# C++ Node-Level GradientEngine — Implementation Plan

## Context

CUEMS needs fade support for ActionCues (fade_in/fade_out). Currently these are stubs that alias to play/stop. The gradient engine must generate smooth parametric curves (sigmoid, bezier, etc.) synchronized to MTC timecode, sending interpolated values via UDP OSC to AudioMixer (volume) and VideoComposer (opacity). After evaluating controller-side, node-side, and player-side approaches, we chose **node-side** for: localhost zero-jitter OSC, fault isolation per node, no player modifications, and natural fit with the existing Controller→Node architecture.

The engine is designed as a general-purpose timecode-driven signal evaluation system (`GradientEngine`). Fade-in/fade-out is the first signal output type, implemented as a sub-component (`FadeRegistry`, `FadeCommand`, `ActiveFade`). The architecture supports future signal output types (e.g., LFOs, envelopes, DMX sequences) without restructuring the core engine.

**Key architectural decisions:**
- Standalone C++ process per node (like VideoComposer), not embedded in NodeEngine
- Joins NNG bus directly as a dialer — receives commands from Controller/NodeEngine
- MTC quarter-frame-driven tick loop (not free-running timer) — evaluations track transport
- Audio fades target AudioPlayer `/volmaster` directly (mixer is reserved for UI volume control)
- Video fades target VideoComposer `/layer/N/opacity` — separate from NodeEngine's `/visible`, `/offset`, `/mtcfollow`
- DMX keeps existing player-side fade mechanism unchanged
- Core library (`libgradient_motion`) with `gme::*` namespace modules, separate from daemon code
- `GradientEngine` is the top-level orchestrator; `Fade*` classes are sub-elements for the fade signal output use case

## Repository: `gradient-motion-engine/`

The source layout follows the README architecture: `libgradient_motion` (the reusable library) organized by `gme::*` namespace modules, and `gradient-motiond` (the daemon) in a separate directory tree.

```
gradient-motion-engine/
├── CMakeLists.txt
├── src/                                    ← libgradient_motion (static library)
│   ├── CMakeLists.txt                      ← collects all module sources
│   ├── gradient/                           ← gme::gradient — keyframes and interpolation
│   │   ├── Curve.h                         ← abstract base
│   │   ├── LinearCurve.h
│   │   ├── SigmoidCurve.h / .cpp           ← parametric: steepness, midpoint
│   │   ├── EaseInCurve.h                   ← power curve: t^exp
│   │   ├── EaseOutCurve.h                  ← 1-(1-t)^exp
│   │   ├── SCurve.h                        ← smoothstep: 3t^2-2t^3
│   │   ├── BezierCurve.h / .cpp            ← cubic bezier, 2 control points
│   │   ├── ScaledCurve.h                   ← decorator: remap input/output ranges
│   │   ├── ResampledCurve.h / .cpp         ← LUT pre-compute + interpolation
│   │   ├── CrossfadePair.h                 ← generates complementary paired values
│   │   └── CurveFactory.h / .cpp           ← string→Curve from JSON params
│   ├── time/                               ← gme::time — timecode, clocks, scheduling
│   │   └── MtcTickSource.h / .cpp          ← wraps MtcReceiver, quarter-frame callback
│   ├── signal/                             ← gme::signal — evaluated value representation
│   │   ├── FadeCommand.h                   ← command data struct
│   │   └── LockFreeQueue.h                 ← SPSC ring buffer (NNG→tick thread)
│   ├── engine/                             ← gme::engine — orchestration and execution pipeline
│   │   ├── ActiveFade.h                    ← one running fade instance
│   │   ├── FadeRegistry.h / .cpp           ← active fade map + tick evaluation
│   │   └── GradientEngine.h / .cpp          ← wires subsystems, owns tick loop
│   ├── osc/                                ← gme::osc — OSC encoding and transport
│   │   └── OscSender.h / .cpp              ← liblo UDP OSC wrapper
│   └── motion/                             ← gme::motion — trajectories (future)
│       └── (reserved for future use)
├── daemon/                                 ← gradient-motiond (executable)
│   ├── main.cpp
│   ├── GradientEngineApplication.h / .cpp
│   ├── config/
│   │   └── ConfigurationManager.h / .cpp
│   └── comms/
│       └── NngBusClient.h / .cpp           ← NNG dialer, JSON parse, command queue
├── cuemslogger/                            ← git submodule → github.com/stagesoft/cuemslogger
│   ├── cuemslogger.h / .cpp
│   └── CMakeLists.txt
├── mtcreceiver/                            ← git submodule → github.com/stagesoft/mtcreceiver
│   ├── mtcreceiver.h / .cpp                ←   pinned to commit 63ce3de (RtMidi 5.x fix)
│   └── CMakeLists.txt
├── tests/
│   ├── test_curves.cpp
│   ├── test_fade_registry.cpp
│   └── test_nng_parse.cpp
├── systemd/
│   └── gradient-motiond.service
└── debian/
    └── (packaging files)
```

**Key structural rules:**
- `src/` contains ONLY `libgradient_motion` code — no daemon logic, no systemd, no CLI
- `daemon/` contains ONLY `gradient-motiond` code — links the library but library has no daemon dependencies
- `cuemslogger/` and `mtcreceiver/` are git submodules at repo root (peer siblings for include path compatibility)
- NNG bus client lives in `daemon/comms/` because it is daemon-specific (the library is protocol-agnostic per README design goals)

### Build Dependencies
- C++17, CMake
- `libnng-dev` — NNG C library (nanomsg-next-gen)
- `nlohmann-json3-dev` — JSON parsing (header-only)
- `libtinyxml2-dev` — XML config parsing (`/etc/cuems/settings.xml`)
- `liblo-dev` — OSC sending
- `librtmidi-dev` >= 5.0 — MIDI/MTC reception (RtMidi 5.x compatibility fixed upstream in stagesoft/mtcreceiver#2)
- `libasound2-dev` — ALSA (for MIDI)

---

## Phase 0: Project Scaffold

**Create:** `CMakeLists.txt`, `src/CMakeLists.txt`, `daemon/main.cpp`, `daemon/GradientEngineApplication.h/.cpp`, `daemon/config/ConfigurationManager.h/.cpp`

- C++17, `-Wall -O3 -pthread`
- Two targets: `gradient_motion` (STATIC library from `src/`), `gradient-motiond` (EXECUTABLE from `daemon/`)
- Create `gme::*` module directories under `src/` (time, motion, gradient, signal, osc, engine) — empty placeholders in Phase 0
- Add `mtcreceiver` as git submodule from `https://github.com/stagesoft/mtcreceiver.git`, pinned to commit `63ce3de` (RtMidi 5.x fix). Update to main branch commit after stagesoft/mtcreceiver#2 is merged
- Add `cuemslogger` as git submodule from `https://github.com/stagesoft/cuemslogger.git`
- CLI args: `--midi-port NAME`, `--log-level`, `--conf-path`
- **Configuration via existing XML:** Read all config from `/etc/cuems/settings.xml` (same file NodeEngine uses). Add `<fadeengine>` element to `NodeConfType` in settings.xsd. GradientEngine reads `controller_url`, `nng_hub_port`, `mtc_port` from existing fields, plus new fadeengine-specific settings. C++ XML parsing via tinyxml2. **Note:** XML parsing is deferred to a later phase; Phase 0 uses CLI arguments only.
- **Settings XSD change:** Add to `NodeConfType`:
  ```xml
  <xs:element name="fadeengine" type="cms:FadeEngineType" minOccurs="0" maxOccurs="1" />
  ```
  New type:
  ```xml
  <xs:complexType name="FadeEngineType">
      <xs:sequence>
          <xs:element name="path" type="cms:NonEmptyString" />
          <xs:element name="args" type="xs:string" minOccurs="0" />
          <xs:element name="default_curve" type="xs:string" minOccurs="0" />
          <xs:element name="default_tick_rate" type="xs:positiveInteger" minOccurs="0" />
      </xs:sequence>
  </xs:complexType>
  ```
- **Settings XML addition** (in `<node>` block):
  ```xml
  <fadeengine>
      <path>/usr/bin/gradient-motiond</path>
  </fadeengine>
  ```
- Follow VideoComposer [main.cpp](../cuems-videocomposer/src/cuems_videocomposer/cpp/main.cpp) for application lifecycle pattern. **Note:** VideoComposer's ConfigurationManager reads `.rc` key=value files, NOT XML — do NOT follow that for config parsing. GradientEngine needs its own XML parser using `tinyxml2` (add `libtinyxml2-dev` dependency) to read `/etc/cuems/settings.xml`. Parse the `<node>` block to extract `controller_url`, `nng_hub_port`, `mtc_port`, and `<fadeengine>` sub-element
- **Logging:** CMake option `ENABLE_CUEMS_LOGGER` (default `ON`). When `ON`: compiles and links the `cuemslogger` submodule, defines `-DHAVE_CUEMS_LOGGER`, daemon code uses `CuemsLogger` for structured syslog output. When `OFF`: daemon code falls back to a thin wrapper around `<iostream>` (`std::cerr` for errors/warnings, `std::cout` for info/debug) — no submodule dependency required. In both cases, the logging interface is accessed through a project-internal abstraction header (`daemon/logging.h`) that selects the backend at compile time via `#ifdef HAVE_CUEMS_LOGGER`. mtcreceiver always uses its own built-in stub logger regardless of this flag (avoids needing `cuems_errors.h` shim). Ensure `StandardOutput=journal` in systemd for production log access

---

## Phase 1: Gradient Module (gme::gradient) — independent

**Create:** all files under `src/gradient/`

Core interface:
```cpp
namespace gme::gradient {

class Curve {
public:
    virtual ~Curve() = default;
    virtual double evaluate(double t) const = 0; // t∈[0,1] → [0,1]
};

} // namespace gme::gradient
```

Key classes (all in `gme::gradient` namespace):
- **SigmoidCurve** — `1/(1+exp(-k*(t-m)))`, normalized so f(0)=0, f(1)=1. Params: `steepness` (k), `midpoint` (m)
- **BezierCurve** — cubic with 2 control points, De Casteljau evaluation
- **ScaledCurve** — decorator wrapping any Curve, remaps input [inMin,inMax]→[0,1] and output [0,1]→[outMin,outMax]
- **ResampledCurve** — pre-computes N-sample LUT at construction, `evaluate()` does linear interp into LUT. Default N=256. This is the default wrapper for playback — avoids per-tick transcendentals
- **CrossfadePair** — holds one Curve, returns `{curve.evaluate(t), 1.0 - curve.evaluate(t)}` for guaranteed complementary values
- **CurveFactory** — `createCurve(type_string, json_params)` → `unique_ptr<Curve>`. Wraps in ResampledCurve by default

**Test:** `tests/test_curves.cpp` — boundary values, monotonicity, ScaledCurve range mapping, ResampledCurve accuracy vs inner curve, CrossfadePair sum=1.0

---

## Phase 2: MTC Tick Source (gme::time) — independent

**Create:** `src/time/MtcTickSource.h/.cpp`

Reuse `MtcReceiver` from the `mtcreceiver` git submodule (`../cuems-videocomposer/src/mtcreceiver/` upstream). Key statics: `MtcReceiver::mtcHead` (atomic long, ms), `MtcReceiver::isTimecodeRunning` (atomic bool).

**Extend MtcReceiver** with a user callback. The callback must fire from inside `decodeQuarterFrame()` (mtcreceiver.cpp), NOT from `midiCallback()`. There are two `mtcHead` update sites in `decodeQuarterFrame()`:
1. Per-quarter running update (~line 209): `mtcHead.store(mtcHead.load() + 250 / curFrame.getFps())`
2. Per-complete-frame decode (~line 281): `mtcHead.store(curFrame.toMilliseconds())`

Add `static std::function<void(long)> onQuarterFrame` member. Call it after BOTH update sites. This fires 8 times per video frame (8 quarter frames per MTC frame). At 25fps = 200Hz, at 30fps = 240Hz. Note: this is higher than the 120Hz stated in earlier discussion — the effective fade update rate is limited by the consumer (VideoComposer ~60fps, AudioMixer ~JACK period rate), so sending at 200-240Hz is fine (the consumer applies the latest value per render cycle).

`MtcTickSource` (in `gme::time` namespace) wraps this:
- `void start(const std::string& midiPort)` — opens MIDI, sets callback
- `long getMtcMs()` — reads `MtcReceiver::mtcHead.load()`
- `bool isRunning()` — reads `MtcReceiver::isTimecodeRunning.load()`
- `void setTickCallback(std::function<void(long)>)` — routes to MtcReceiver's static callback

**Note:** MtcReceiver uses static members (one global instance per process). This is fine for GradientEngine (one instance per node), but means the class is not reusable for multiple listeners. Acceptable constraint.

---

## Phase 3: NNG Bus Client — independent

**Create:** `daemon/comms/NngBusClient.h/.cpp`, `src/signal/FadeCommand.h`, `src/signal/LockFreeQueue.h`

NNG bus client lives in `daemon/comms/` (daemon-specific, not part of `libgradient_motion`). FadeCommand and LockFreeQueue live in `src/signal/` (part of the library — reusable data structures).

Uses **nng C library** (not pynng). Matches existing protocol:
- Socket: `nng_bus0_open()` + `nng_dial()` with **non-blocking dial** (`NNG_FLAG_NONBLOCK`) and reconnect params (min 1s, max 30s) — matching pynng behavior in HubServices.py line 217
- Connect to: `tcp://{controller_url}:{nng_port}` (default 9093)
- Receive: background thread, `nng_recv()` loop, parse JSON with nlohmann/json
- Filter: only process messages where `target == "gradientengine"` — check early before full JSON parse. `gradientengine` is the uniform inbound target for this daemon; the fade-specific kind is discriminated by `data.command` (`start_fade` / `cancel_fade` / `cancel_all` / `start_crossfade`). Future non-fade message types on the same target will add new `data.command` values, not new `target` values.
- Send: `nng_send()` for status. Outbound envelopes use `target: "gradientengine"` uniformly; the status kind lives in `data.event` (`fade_complete` or `fade_error`).

**FadeCommand struct** (in `gme::signal` namespace):
```cpp
namespace gme::signal {

struct FadeCommand {
    enum Type { START_FADE, CANCEL_FADE, CANCEL_ALL, START_CROSSFADE };
    Type type;
    std::string fade_id;
    std::string osc_host;       // usually "127.0.0.1"
    int osc_port;               // AudioMixer port or 7000 (VideoComposer)
    std::string osc_path;       // "/volmaster" (AudioPlayer) or "/videocomposer/layer/X/opacity"
    float start_value, end_value;
    float duration_ms;
    std::string curve_type;
    nlohmann::json curve_params;
    long start_mtc_ms;          // -1 = start at current MTC position (NOT 0, since 0 is valid MTC)
    // For crossfade:
    std::string partner_fade_id;
    std::string partner_osc_path;
    float partner_start_value, partner_end_value;
};

} // namespace gme::signal
```

**NNG message format** (matches existing NodeOperation JSON protocol):
```json
{
  "type": "command", "action": "update",
  "sender": "controller", "target": "gradientengine",
  "data": {
    "command": "start_fade",
    "fade_id": "fade_abc123",
    "node_name": "nodeA",
    "osc_path": "/volmaster",
    "osc_port": 9234, "osc_host": "127.0.0.1",
    "start_value": 0.0, "end_value": 1.0,
    "duration_ms": 3000,
    "curve_type": "sigmoid",
    "curve_params": {"steepness": 8.0},
    "start_mtc_ms": -1
  }
}
```

**Wire/struct key parity**: the JSON key is `osc_path` (and `partner_osc_path`
for crossfade) — identical to the C++ struct field name. Earlier drafts used
`osc_address` on the wire; that rename is no longer in effect, and the
Python-side `ActionHandler` emitter (Phase 6) MUST use `osc_path`.

**LockFreeQueue** (in `gme::signal` namespace): SPSC ring buffer (NNG thread → tick thread). Fixed capacity 64. **On overflow: drop oldest** (not block) — prevents NNG thread from stalling. Log a warning on drop.

**Fallback drain when MTC stopped:** GradientEngine runs a low-frequency timer (every 100ms) that drains the command queue even when no MTC ticks are firing. This prevents command loss when MTC is stopped. The timer thread only drains commands (add/cancel fades), it does NOT evaluate curves or send OSC (no ticking without MTC). This ensures `CANCEL_ALL` on project unload is always processed.

**Phase 3 scope boundary** (see `specs/005-nng-bus-client/spec.md` §Scope Boundaries for the authoritative list):

- In-scope: NNG socket dial + recv + filter + parse + enqueue; `LockFreeQueue`; 100 ms fallback drain; `NngBusClient::sendStatus` API; parse-error `fade_error` emission.
- Deferred to Phase 4: `fade_complete` emission at t ≥ 1.0 (needs `FadeRegistry::tick`); `fade_error` on OSC send failure (needs `OscSender`).
- Deferred to Phase 5: SIGTERM graceful shutdown sequence (needs `FadeRegistry` enumeration + `GradientEngineApplication` signal handler); per-fade `fade_error` on daemon shutdown; 2 s exit budget.

**Test:** `tests/test_nng_parse.cpp` — parse sample JSON, verify FadeCommand fields.
`tests/test_lockfree_queue.cpp` — SPSC correctness, drop-oldest, TSan clean, 200 ms fallback-drain timing.
`tests/test_nng_integration.cpp` — loopback NNG pair for NNG→queue latency, sustained-load drop rate, and reconnect-after-disconnect (SC-001, SC-003, SC-006).

---

## Phase 4: Fade Registry + Tick Loop (gme::engine + gme::osc) — depends on 1, 2, 3

**Create:** `src/engine/ActiveFade.h`, `src/engine/FadeRegistry.h/.cpp`, `src/engine/GradientEngine.h/.cpp`, `src/osc/OscSender.h/.cpp`

**ActiveFade** (in `gme::engine` namespace):
```cpp
namespace gme::engine {

struct ActiveFade {
    std::string fade_id;
    std::unique_ptr<gme::gradient::Curve> curve;  // pre-resampled from CurveFactory
    lo_address osc_target;            // liblo address, created once
    std::string osc_path;
    float start_value, end_value;
    long start_mtc_ms, duration_ms;
    float last_sent_value;            // track current value for cancel/crash recovery
    bool completed = false;
    ActiveFade* crossfade_partner = nullptr;
};

} // namespace gme::engine
```

**FadeRegistry::tick(long mtc_ms):**
1. For each active fade: `t = clamp((mtc_ms - start_mtc_ms) / duration_ms, 0, 1)`
2. `value = start_value + (end_value - start_value) * curve->evaluate(t)`
3. `int ret = lo_send(osc_target, osc_path, "f", value)` — check return, log on error, count consecutive failures
4. `last_sent_value = value`
5. If `t >= 1.0`: mark completed, enqueue a status-emit request for the out-of-band status worker (see below) — implements spec FR-006a
6. On repeated OSC send failure attributed to this fade: enqueue a `fade_error` status-emit request with `reason: "osc_send_failed"` — implements spec FR-006b (OSC branch)

**Status-emit worker** (Phase 4 addition, driven by Principle IV — Real-Time Safety): `NngBusClient::sendStatus` performs a blocking `nng_send` and therefore MUST NOT be called directly from the MTC tick thread. Phase 4 introduces a lightweight outbound-status worker thread owned by `NngBusClient` with its own in-class bounded queue (fixed capacity, same SPSC-style overflow behaviour as the inbound queue — drop-oldest with warning). `FadeRegistry::tick` pushes `(StatusKind, fade_id, reason)` tuples onto this queue in the tick thread; the worker thread pops them and calls `sendStatus` out of band. This preserves the tick thread's real-time safety contract while keeping the `sendStatus` API the single serialisation point for NNG output. The parse-error caller (Phase 3 `NngBusClient::recvLoop`) continues to call `sendStatus` directly — the recv thread has no real-time budget.

**FadeRegistry::cancelFade(fade_id, snap_to_end):**
- If `snap_to_end == true`: send one final OSC with `end_value` (for stop/disarm scenarios)
- If `snap_to_end == false`: parameter stays at `last_sent_value` (for manual cancel)

**FadeRegistry::cancelAll():**
- Cancel all active fades. Called on project unload/stop. Does NOT send final values (the players are about to be reset/killed anyway).

**GradientEngine — three threads:**
1. **NNG thread:** `NngBusClient` recv loop → pushes FadeCommands into LockFreeQueue
2. **MTC tick thread:** On quarter-frame callback from MtcTickSource:
   - Drain LockFreeQueue → apply to FadeRegistry (addFade/cancelFade)
   - `FadeRegistry::tick(mtc_ms)`
   - Remove completed fades
3. **Fallback drain thread:** 100ms timer, drains queue when MTC is not ticking (for CANCEL_ALL and commands arriving while MTC is stopped)

**MTC transport behavior:**
- MTC stops → `isTimecodeRunning` false → MTC tick stops firing → fades hold at `last_sent_value` (automatic). Fallback drain thread still processes new commands.
- MTC resumes → ticks resume → fades continue from MTC-relative position (automatic, since evaluation uses absolute MTC time)
- MTC rewind → fade re-evaluates at new position. If `mtc_ms < start_mtc_ms`, `t = 0`, parameter goes to `start_value`. **Note:** completed fades are already removed from registry and will NOT replay on rewind. This is acceptable — rewind in a live show is an exceptional event, and the operator would re-trigger the cue.

**Test:** `tests/test_fade_registry.cpp` — mock MTC, add fade, verify curve values at known MTC positions, verify completion, verify cancel behavior (snap vs hold)

---

## Phase 5: Application + systemd — depends on 4

**Wire up** `GradientEngineApplication::initialize()` (in `daemon/`) → create MtcTickSource, NngBusClient, FadeRegistry, GradientEngine. `run()` → start threads, block on signal. `shutdown()` → cancel all, close NNG, close MIDI.

**Graceful shutdown sequence** (spec FR-013, SC-008): the SIGTERM/SIGINT handler installed by `GradientEngineApplication` MUST:

1. Stop accepting new commands from the NNG receive thread (set a shutdown flag consulted in `recvLoop`; do not call `NngBusClient::stop()` yet).
2. Enumerate active fades via `FadeRegistry`; for each, call `NngBusClient::sendStatus(FadeError, fade_id, "daemon_shutdown")` directly from the shutdown thread (not via the tick-thread status worker — the tick thread is being torn down).
3. Halt fade evaluation without sending any final OSC values — parameters remain at `last_sent_value`.
4. Call `NngBusClient::stop()` to close the socket and join the receive thread.
5. Exit within 2 s of signal receipt (overall budget).

The `sendStatus` calls in step 2 are safe from the shutdown thread because `nng_send` is thread-safe on a single NNG socket (see `specs/005-nng-bus-client/research.md` Decision 5) and the tick thread is not competing for it at that point.

**systemd:** `gradient-motiond.service`
```ini
[Unit]
Description=CUEMS Gradient Motion Engine (MTC-synced movement processor)
PartOf=cuems-node.target
After=cuems-node-engine.service jackd-cuems.service

[Service]
Type=simple
User=cuems
Group=cuems
SupplementaryGroups=audio
ExecStart=/usr/bin/gradient-motiond --conf-path /etc/cuems
Restart=on-failure
RestartSec=3
StandardOutput=journal
StandardError=journal
SyslogIdentifier=gradient-motiond

[Install]
WantedBy=cuems-node.target
```

GradientEngine reads `controller_url` and `nng_hub_port` from `/etc/cuems/settings.xml` (same as NodeEngine). No separate env file needed.

Also add to cuems-common service collection.

---

## Phase 6: Python-Side Changes — PREREQUISITE: 6b-NodeEngine filter must deploy BEFORE gradient-motiond runs

### 6a: cuems-utils

**Modify** [ActionCue.py](../cuems-utils/src/cuemsutils/cues/ActionCue.py):
- Add to `REQ_ITEMS`: `'fade_time': None`, `'fade_curve': None`, `'fade_curve_params': None`
- Add property pairs for each (same pattern as `action_target`/`action_type`)
- Default `fade_curve` to `'linear'` when `fade_time` is set but `fade_curve` is None

**Modify** XSD schemas:
- `script.xsd`: add optional `fade_time` (float), `fade_curve` (string), `fade_curve_params` (string) to ActionCueType. Existing saved projects without these fields will load with None values (backward compatible).
- `settings.xsd`: add `GradientEngineType` and `<gradientengine>` element to `NodeConfType` (see Phase 0)

### 6b: cuems-engine (DEPLOY ORDER: NodeEngine filter FIRST)

**Modify** [NodeCommunications.py](../cuems-engine/src/cuemsengine/comms/NodeCommunications.py) — **PREREQUISITE, deploy before gradient-motiond**:
- In `_handle_command_operation` (line 60): add early return `if operation.target == "gradientengine": return` at line 73, before `command_name = operation.target`. This is the correct layer — the full `NodeOperation` with the `target` field is available here, unlike in `NodeEngine._handle_nng_command` which only receives `(command_name, value, address)`. The filter is on the uniform target `"gradientengine"` regardless of which fade sub-command the message carries inside `data`.
- Also add `OperationType.STATUS` handler to `set_receive_callbacks` (line 35-37) for receiving `data.event: "fade_complete"` status messages from gradient-motiond (envelope target is `"gradientengine"`; the handler inspects `data.event`).

**Modify** [ControllerEngine.py](../cuems-engine/src/cuemsengine/ControllerEngine.py):
- In STATUS handler: ignore/skip status messages from gradient-motiond (`sender.startswith("gradientengine_")`) to prevent confusion in multi-node setups where Bus0 broadcasts to all peers

**Modify** [ActionHandler.py](../cuems-engine/src/cuemsengine/cues/ActionHandler.py) `_handle_fade_in` (line 402):
1. Arm target (existing)
2. Set `target._fade_initial_volume = 0.0` on the cue object (side-channel for run_cue)
3. Go target — `ch.go(target, mtc)` launches `go_threaded` in background thread
4. Resolve target's OSC endpoint (after arm, before go returns):
   - **AudioCue** → AudioPlayer's OSC port + `/volmaster`. Port is at `target._osc.remote_port` (set during arm by `PlayerHandler.new_audio_output()` → `PlayerClient(remote_port=port)`).
   - **VideoCue** → port 7000 + `/videocomposer/layer/{layer_id}/opacity`. Layer ID in `target._layer_ids` after arm.
5. Send NNG `NodeOperation(COMMAND, UPDATE, target="gradientengine", data={command: "start_fade", ...})` via `CUE_HANDLER.communications_thread` — the fade sub-kind lives in `data.command`, not on the envelope's `target`
6. **Unit conversion:** `start_value=0.0, end_value=float(target.master_vol) / 100.0` (UI uses 0-100%, OSC expects 0.0-1.0 — see run_cue.py line 140)

**Volume conflict with run_cue:** `run_audioCue` (run_cue.py line 134-150) sets `/volmaster` once at cue start. Use a **side-channel on the cue object** to override:
- ActionHandler sets `target._fade_initial_volume = 0.0` before calling `ch.go()`
- Modify `run_audioCue` to check `getattr(cue, '_fade_initial_volume', None)` — if set, use that value instead of `cue.master_vol / 100.0` for the initial `/volmaster` write, then delete the attribute
- This avoids changing the `@singledispatch` signature or `go_threaded` parameters
- **Race condition:** `go_threaded` runs `run_audioCue` in a background thread. ActionHandler sends NNG to gradient-motiond after `ch.go()` returns. Since `run_audioCue` sets vol=0 first and gradient-motiond starts on the next MTC quarter frame (up to 5ms later), the initial vol=0 is set before the first tick. Acceptable ordering.

**Modify** `_handle_fade_out` (line 417):
1. Resolve start_value: `float(target.master_vol) / 100.0` — the cue's configured volume as 0.0-1.0 gain. This matches what `run_audioCue` initially set. (If a previous fade changed the volume, track the last-known value on the cue: `getattr(target, '_current_volume', float(target.master_vol) / 100.0)`)
2. Resolve port: same as fade_in — `target._osc.remote_port`
3. Send NNG start_fade with `start_value=current_vol, end_value=0.0`
4. On `fade_complete` NNG status from gradient-motiond → trigger disarm. **Implementation:** Add `OperationType.STATUS` to `NodeCommunications.set_receive_callbacks()` (currently only registers COMMAND — line 35-37). The new STATUS handler checks `operation.target == "gradientengine"` AND `operation.data.get("event") == "fade_complete"` (the envelope target is always `"gradientengine"`; the fade-specific discriminator is `data.event`), then calls `CUE_HANDLER.disarm(cue_by_fade_id)`. The `fade_id` carried in `data.fade_id` maps back to the target cue.

**Modify** [CueHandler.py](../cuems-engine/src/cuemsengine/cues/CueHandler.py) pre-arm path (~line 267):
- Add `fade_in` to the pre-arm condition alongside `play`. Currently only `action_type == 'play'` triggers pre-arm of the ActionCue's target at script load. Since `fade_in` also starts playback (from silence), the target should be pre-armed to avoid arm-at-go-time delay:
  ```python
  if isinstance(cue, ActionCue) and cue._action_target_object:
      if cue.action_type in ('play', 'fade_in'):
          self.arm(cue._action_target_object, init)
  ```

**Modify** project load/stop flow:
- In `ControllerEngine.load_project()` and `ControllerEngine.stop_script()`: send `CANCEL_ALL` to gradient-motiond via NNG before killing players. This prevents stale fades from sending OSC to dead player ports.

**Modify** [NodesHub.py](../cuems-engine/src/cuemsengine/comms/NodesHub.py):
- No new OperationType needed — reuse `COMMAND` with `target="gradientengine"` (uniform for all inbound messages destined to gradient-motiond). The fade-specific sub-kind is inside `data.command`; GradientEngine filters on the target field first, then dispatches on `data.command`.

---

## Phase 7: Crossfade Support — depends on 4, 6

- `START_CROSSFADE` command includes both fade targets in one message
- `FadeRegistry` links paired `ActiveFade` entries
- Uses `CrossfadePair` from `gme::gradient` module for complementary values
- ActionHandler detects when fade_in targets same output as existing fade_out → sends crossfade command

---

## Reuse from VideoComposer

| Component | Source | Strategy |
|-----------|--------|----------|
| MtcReceiver | `github.com/stagesoft/mtcreceiver` | Git submodule, pinned to `63ce3de` (RtMidi 5.x fix). Extend with quarter-frame callback in Phase 2 (upstream PR or fork if needed) |
| CuemsLogger | `github.com/stagesoft/cuemslogger` | Git submodule. Used directly by daemon code; mtcreceiver uses its built-in stub |
| ConfigurationManager pattern | `../cuems-videocomposer/.../config/ConfigurationManager.h` | Follow pattern, simpler version in `daemon/config/` |
| Application lifecycle | `VideoComposerApplication` | Follow init/run/shutdown pattern in `daemon/` |
| liblo OSC patterns | `OSCRemoteControl.cpp` | Same `lo_send`/`lo_address` usage in `src/osc/` |
| systemd service | `cuems-videocomposer.service` | Follow same structure for `gradient-motiond.service` |

**Note on mtcreceiver:** Both `gradient-motion-engine` and `cuems-videocomposer` use `mtcreceiver` as a git submodule from the same upstream repo. The quarter-frame callback extension (Phase 2) should be contributed upstream to `stagesoft/mtcreceiver` to keep both consumers on the same codebase.

## Phase Dependency Graph

```
Phase 0 (scaffold)
  ├── Phase 1 (gme::gradient)  ─┐
  ├── Phase 2 (gme::time)      ─┼── all independent, build in parallel
  └── Phase 3 (NNG + signal)   ─┘
           │
       Phase 4 (gme::engine + gme::osc)
           │
       Phase 5 (daemon wiring + systemd)
           │
       Phase 6 (Python changes)  ← 6b NodeEngine filter MUST deploy before gradient-motiond runs
           │
       Phase 7 (crossfade)
```

## Known Limitations / Future Work

- **Crash mid-fade:** Parameter stays at `last_sent_value` until restart. Future: NodeEngine could detect gradient-motiond absence (NNG heartbeat or systemd watchdog) and reset parameters.
- **UI/Editor fade progress:** No mechanism for displaying active fade status in the frontend. Future: gradient-motiond could send periodic progress updates on NNG.
- **Completed fades don't replay on MTC rewind:** Acceptable for live performance (rewind is exceptional). Future: keep completed fades in a "cold" list for rewind replay.
- **Multi-node bus pollution:** GradientEngine status messages are broadcast to all NNG peers. Mitigated by target filtering in NodeEngine (Phase 6b) and ControllerEngine. Volume is low (one message per fade completion).
- **Video opacity priority:** If NodeEngine and GradientEngine both send opacity to the same layer simultaneously, last-write-wins via UDP. In practice, NodeEngine sends `/visible` (binary) and GradientEngine sends `/opacity` (continuous) — different parameters. If future need arises, add a priority lock in VideoComposer's OSC handler.
- **AudioPlayer port lifetime:** AudioPlayer ports are random and tied to the player process lifetime. If the AudioPlayer dies mid-fade (crash, disarm by another cue), GradientEngine sends OSC to a dead port (silent UDP failure). The `fade_complete` will still fire when duration elapses, but the fade has no effect. Acceptable — same behavior as any OSC-to-dead-player scenario.
- **Mixer vs GradientEngine:** The UI controls volume via AudioMixer (`/audiomixer/{name}/{ch}`), while fades control volume via AudioPlayer (`/volmaster`). These are independent gain stages — both are applied to the audio output. A fade to 0.0 silences even if mixer is at 1.0. This is correct behavior (fade is an artistic effect, mixer is an operator control).

## Verification

1. **Unit tests:** `cmake --build build --target test` — curves, registry, NNG parsing
2. **Integration test:** Start gradient-motiond + libmtcmaster (MTC generator) + OSC listener on loopback → verify values match expected curve at expected MTC positions
3. **Manual test:** Full stack (gradient-motiond + VideoComposer + AudioMixer + NodeEngine + Controller) → trigger ActionCue with fade_in → observe smooth volume/opacity transition
4. **Transport test:** Pause MTC mid-fade → verify fade holds. Resume → verify fade continues. Stop → verify fades cancel.
5. **Cancel test:** Project unload while fade is active → verify CANCEL_ALL arrives and fades stop before mixer reset
6. **NNG filter test:** Send fade command, verify NodeEngine does NOT log error, gradient-motiond receives and processes
7. **Crossfade test:** Simultaneous fade_out + fade_in on same audio channel → verify no volume dip/overshoot (complementary values sum to ~1.0)
