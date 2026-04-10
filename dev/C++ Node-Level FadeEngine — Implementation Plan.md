# C++ Node-Level FadeEngine — Implementation Plan

## Context

CUEMS needs fade support for ActionCues (fade_in/fade_out). Currently these are stubs that alias to play/stop. The fade engine must generate smooth parametric curves (sigmoid, bezier, etc.) synchronized to MTC timecode, sending interpolated values via UDP OSC to AudioMixer (volume) and VideoComposer (opacity). After evaluating controller-side, node-side, and player-side approaches, we chose **node-side** for: localhost zero-jitter OSC, fault isolation per node, no player modifications, and natural fit with the existing Controller→Node architecture.

**Key architectural decisions:**
- Standalone C++ process per node (like VideoComposer), not embedded in NodeEngine
- Joins NNG bus directly as a dialer — receives fade commands from Controller/NodeEngine
- MTC quarter-frame-driven tick loop (not free-running timer) — fades track transport
- Audio fades target AudioPlayer `/volmaster` directly (mixer is reserved for UI volume control)
- Video fades target VideoComposer `/layer/N/opacity` — separate from NodeEngine's `/visible`, `/offset`, `/mtcfollow`
- DMX keeps existing player-side fade mechanism unchanged
- C++ curve library as separate static lib for reuse

## New Repository: `gradient-motion-engine/`

```
gradient-motion-engine/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── FadeEngineApplication.h / .cpp
│   ├── config/
│   │   └── ConfigurationManager.h / .cpp
│   ├── curves/                          ← libcuemscurves (static library)
│   │   ├── Curve.h                      ← abstract base
│   │   ├── LinearCurve.h
│   │   ├── SigmoidCurve.h / .cpp        ← parametric: steepness, midpoint
│   │   ├── EaseInCurve.h                ← power curve: t^exp
│   │   ├── EaseOutCurve.h               ← 1-(1-t)^exp
│   │   ├── SCurve.h                     ← smoothstep: 3t^2-2t^3
│   │   ├── BezierCurve.h / .cpp         ← cubic bezier, 2 control points
│   │   ├── ScaledCurve.h                ← decorator: remap input/output ranges
│   │   ├── ResampledCurve.h / .cpp      ← LUT pre-compute + interpolation
│   │   ├── CrossfadePair.h              ← generates complementary paired values
│   │   └── CurveFactory.h / .cpp        ← string→Curve from JSON params
│   ├── sync/
│   │   └── MtcTickSource.h / .cpp       ← wraps MtcReceiver, quarter-frame callback
│   ├── comms/
│   │   ├── NngBusClient.h / .cpp        ← NNG dialer, JSON parse, command queue
│   │   ├── FadeCommand.h                ← command data struct
│   │   └── LockFreeQueue.h              ← SPSC ring buffer (NNG→tick thread)
│   ├── engine/
│   │   ├── ActiveFade.h                 ← one running fade instance
│   │   ├── FadeRegistry.h / .cpp        ← active fade map + tick evaluation
│   │   └── FadeEngine.h / .cpp          ← wires subsystems, owns tick loop
│   └── osc/
│       └── OscSender.h / .cpp           ← liblo UDP OSC wrapper
├── mtcreceiver/                         ← copied from cuems-videocomposer
│   ├── mtcreceiver.h / .cpp
│   └── CMakeLists.txt
├── tests/
│   ├── test_curves.cpp
│   ├── test_fade_registry.cpp
│   └── test_nng_parse.cpp
├── systemd/
│   └── gradient-motion-engine.service
└── debian/
    └── (packaging files)
```

### Build Dependencies
- C++17, CMake
- `libnng-dev` — NNG C library (nanomsg-next-gen)
- `nlohmann-json3-dev` — JSON parsing (header-only)
- `libtinyxml2-dev` — XML config parsing (`/etc/cuems/settings.xml`)
- `liblo-dev` — OSC sending
- `librtmidi-dev` >= 5.0 — MIDI/MTC reception (note: RtMidi 5.x renamed `LINUX_ALSA` → `LINUX_ALSA_SEQ`; mtcreceiver sources must be patched accordingly)
- `libasound2-dev` — ALSA (for MIDI)

---

## Phase 0: Project Scaffold

**Create:** `CMakeLists.txt`, `src/main.cpp`, `src/FadeEngineApplication.h/.cpp`, `src/config/ConfigurationManager.h/.cpp`

- C++17, `-Wall -O3 -pthread`
- Two targets: `cuemscurves` (STATIC), `gradient-motion-engine` (EXECUTABLE)
- Copy `mtcreceiver/` from `/home/stagelab/src/cuems-videocomposer/src/mtcreceiver/` as subdirectory. Patch for RtMidi 5.x API compatibility (`LINUX_ALSA` → `LINUX_ALSA_SEQ`)
- CLI args: `--midi-port NAME`, `--log-level` (overrides)
- **Configuration via existing XML:** Read all config from `/etc/cuems/settings.xml` (same file NodeEngine uses). Add `<fadeengine>` element to `NodeConfType` in settings.xsd. FadeEngine reads `controller_url`, `nng_hub_port`, `mtc_port` from existing fields, plus new fadeengine-specific settings. C++ XML parsing via libxml2 or tinyxml2.
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
      <path>/usr/bin/gradient-motion-engine</path>
  </fadeengine>
  ```
- Follow VideoComposer [main.cpp](../cuems-videocomposer/src/cuems_videocomposer/cpp/main.cpp) for application lifecycle pattern. **Note:** VideoComposer's ConfigurationManager reads `.rc` key=value files, NOT XML — do NOT follow that for config parsing. FadeEngine needs its own XML parser using `tinyxml2` (add `libtinyxml2-dev` dependency) or `libxml2` to read `/etc/cuems/settings.xml`. Parse the `<node>` block to extract `controller_url`, `nng_hub_port`, `mtc_port`, and `<fadeengine>` sub-element
- Logging: use CuemsLogger if available (`#ifdef HAVE_CUEMS_LOGGER`), stub to stderr/journal otherwise. Ensure `StandardOutput=journal` in systemd for production log access

---

## Phase 1: Curve Library (libcuemscurves) — independent

**Create:** all files under `src/curves/`

Core interface:
```cpp
class Curve {
public:
    virtual ~Curve() = default;
    virtual double evaluate(double t) const = 0; // t∈[0,1] → [0,1]
};
```

Key classes:
- **SigmoidCurve** — `1/(1+exp(-k*(t-m)))`, normalized so f(0)=0, f(1)=1. Params: `steepness` (k), `midpoint` (m)
- **BezierCurve** — cubic with 2 control points, De Casteljau evaluation
- **ScaledCurve** — decorator wrapping any Curve, remaps input [inMin,inMax]→[0,1] and output [0,1]→[outMin,outMax]
- **ResampledCurve** — pre-computes N-sample LUT at construction, `evaluate()` does linear interp into LUT. Default N=256. This is the default wrapper for playback — avoids per-tick transcendentals
- **CrossfadePair** — holds one Curve, returns `{curve.evaluate(t), 1.0 - curve.evaluate(t)}` for guaranteed complementary values
- **CurveFactory** — `createCurve(type_string, json_params)` → `unique_ptr<Curve>`. Wraps in ResampledCurve by default

**Test:** `tests/test_curves.cpp` — boundary values, monotonicity, ScaledCurve range mapping, ResampledCurve accuracy vs inner curve, CrossfadePair sum=1.0

---

## Phase 2: MTC Tick Source — independent

**Create:** `src/sync/MtcTickSource.h/.cpp`

Reuse `MtcReceiver` from `/home/stagelab/src/cuems-videocomposer/src/mtcreceiver/mtcreceiver.h` (copy sources). Key statics: `MtcReceiver::mtcHead` (atomic long, ms), `MtcReceiver::isTimecodeRunning` (atomic bool).

**Extend MtcReceiver** with a user callback. The callback must fire from inside `decodeQuarterFrame()` (mtcreceiver.cpp), NOT from `midiCallback()`. There are two `mtcHead` update sites in `decodeQuarterFrame()`:
1. Per-quarter running update (~line 209): `mtcHead.store(mtcHead.load() + 250 / curFrame.getFps())`
2. Per-complete-frame decode (~line 281): `mtcHead.store(curFrame.toMilliseconds())`

Add `static std::function<void(long)> onQuarterFrame` member. Call it after BOTH update sites. This fires 8 times per video frame (8 quarter frames per MTC frame). At 25fps = 200Hz, at 30fps = 240Hz. Note: this is higher than the 120Hz stated in earlier discussion — the effective fade update rate is limited by the consumer (VideoComposer ~60fps, AudioMixer ~JACK period rate), so sending at 200-240Hz is fine (the consumer applies the latest value per render cycle).

`MtcTickSource` wraps this:
- `void start(const std::string& midiPort)` — opens MIDI, sets callback
- `long getMtcMs()` — reads `MtcReceiver::mtcHead.load()`
- `bool isRunning()` — reads `MtcReceiver::isTimecodeRunning.load()`
- `void setTickCallback(std::function<void(long)>)` — routes to MtcReceiver's static callback

**Note:** MtcReceiver uses static members (one global instance per process). This is fine for FadeEngine (one instance per node), but means the class is not reusable for multiple listeners. Acceptable constraint.

---

## Phase 3: NNG Bus Client — independent

**Create:** `src/comms/NngBusClient.h/.cpp`, `src/comms/FadeCommand.h`, `src/comms/LockFreeQueue.h`

Uses **nng C library** (not pynng). Matches existing protocol:
- Socket: `nng_bus0_open()` + `nng_dial()` with **non-blocking dial** (`NNG_FLAG_NONBLOCK`) and reconnect params (min 1s, max 30s) — matching pynng behavior in HubServices.py line 217
- Connect to: `tcp://{controller_url}:{nng_port}` (default 9093)
- Receive: background thread, `nng_recv()` loop, parse JSON with nlohmann/json
- Filter: only process messages where `target == "fadeengine"` — check early before full JSON parse
- Send: `nng_send()` for status (fade_complete, fade_error)

**FadeCommand struct:**
```cpp
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
```

**NNG message format** (matches existing NodeOperation JSON protocol):
```json
{
  "type": "command", "action": "update",
  "sender": "controller", "target": "fadeengine",
  "data": {
    "command": "start_fade",
    "fade_id": "fade_abc123",
    "osc_address": "/volmaster",
    "osc_port": 9234, "osc_host": "127.0.0.1",
    "start_value": 0.0, "end_value": 1.0,
    "duration_ms": 3000,
    "curve_type": "sigmoid",
    "curve_params": {"steepness": 8.0},
    "start_mtc_ms": -1
  }
}
```

**LockFreeQueue:** SPSC ring buffer (NNG thread → tick thread). Fixed capacity 64. **On overflow: drop oldest** (not block) — prevents NNG thread from stalling. Log a warning on drop.

**Fallback drain when MTC stopped:** FadeEngine runs a low-frequency timer (every 100ms) that drains the command queue even when no MTC ticks are firing. This prevents command loss when MTC is stopped. The timer thread only drains commands (add/cancel fades), it does NOT evaluate curves or send OSC (no ticking without MTC). This ensures `CANCEL_ALL` on project unload is always processed.

**Test:** `tests/test_nng_parse.cpp` — parse sample JSON, verify FadeCommand fields

---

## Phase 4: Fade Registry + Tick Loop — depends on 1, 2, 3

**Create:** `src/engine/ActiveFade.h`, `src/engine/FadeRegistry.h/.cpp`, `src/engine/FadeEngine.h/.cpp`, `src/osc/OscSender.h/.cpp`

**ActiveFade:**
```cpp
struct ActiveFade {
    std::string fade_id;
    std::unique_ptr<Curve> curve;     // pre-resampled from CurveFactory
    lo_address osc_target;            // liblo address, created once
    std::string osc_path;
    float start_value, end_value;
    long start_mtc_ms, duration_ms;
    float last_sent_value;            // track current value for cancel/crash recovery
    bool completed = false;
    ActiveFade* crossfade_partner = nullptr;
};
```

**FadeRegistry::tick(long mtc_ms):**
1. For each active fade: `t = clamp((mtc_ms - start_mtc_ms) / duration_ms, 0, 1)`
2. `value = start_value + (end_value - start_value) * curve->evaluate(t)`
3. `int ret = lo_send(osc_target, osc_path, "f", value)` — check return, log on error, count consecutive failures
4. `last_sent_value = value`
5. If `t >= 1.0`: mark completed, queue NNG status message

**FadeRegistry::cancelFade(fade_id, snap_to_end):**
- If `snap_to_end == true`: send one final OSC with `end_value` (for stop/disarm scenarios)
- If `snap_to_end == false`: parameter stays at `last_sent_value` (for manual cancel)

**FadeRegistry::cancelAll():**
- Cancel all active fades. Called on project unload/stop. Does NOT send final values (the players are about to be reset/killed anyway).

**FadeEngine — three threads:**
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

**Wire up** `FadeEngineApplication::initialize()` → create MtcTickSource, NngBusClient, FadeRegistry, FadeEngine. `run()` → start threads, block on signal. `shutdown()` → cancel all, close NNG, close MIDI.

**systemd:** `gradient-motion-engine.service`
```ini
[Unit]
Description=CUEMS Fade Engine (MTC-synced fade processor)
PartOf=cuems-node.target
After=cuems-node-engine.service jackd-cuems.service

[Service]
Type=simple
User=cuems
Group=cuems
SupplementaryGroups=audio
ExecStart=/usr/bin/gradient-motion-engine --conf-path /etc/cuems
Restart=on-failure
RestartSec=3
StandardOutput=journal
StandardError=journal
SyslogIdentifier=gradient-motion-engine

[Install]
WantedBy=cuems-node.target
```

FadeEngine reads `controller_url` and `nng_hub_port` from `/etc/cuems/settings.xml` (same as NodeEngine). No separate env file needed.

Also add to cuems-common service collection.

---

## Phase 6: Python-Side Changes — PREREQUISITE: 6b-NodeEngine filter must deploy BEFORE FadeEngine runs

### 6a: cuems-utils

**Modify** [ActionCue.py](../cuems-utils/src/cuemsutils/cues/ActionCue.py):
- Add to `REQ_ITEMS`: `'fade_time': None`, `'fade_curve': None`, `'fade_curve_params': None`
- Add property pairs for each (same pattern as `action_target`/`action_type`)
- Default `fade_curve` to `'linear'` when `fade_time` is set but `fade_curve` is None

**Modify** XSD schemas:
- `script.xsd`: add optional `fade_time` (float), `fade_curve` (string), `fade_curve_params` (string) to ActionCueType. Existing saved projects without these fields will load with None values (backward compatible).
- `settings.xsd`: add `FadeEngineType` and `<fadeengine>` element to `NodeConfType` (see Phase 0)

### 6b: cuems-engine (DEPLOY ORDER: NodeEngine filter FIRST)

**Modify** [NodeCommunications.py](../cuems-engine/src/cuemsengine/comms/NodeCommunications.py) — **PREREQUISITE, deploy before FadeEngine**:
- In `_handle_command_operation` (line 60): add early return `if operation.target == "fadeengine": return` at line 73, before `command_name = operation.target`. This is the correct layer — the full `NodeOperation` with the `target` field is available here, unlike in `NodeEngine._handle_nng_command` which only receives `(command_name, value, address)`.
- Also add `OperationType.STATUS` handler to `set_receive_callbacks` (line 35-37) for receiving fade_complete messages from FadeEngine.

**Modify** [ControllerEngine.py](../cuems-engine/src/cuemsengine/ControllerEngine.py):
- In STATUS handler: ignore/skip status messages from FadeEngine (`sender.startswith("fadeengine_")`) to prevent confusion in multi-node setups where Bus0 broadcasts to all peers

**Modify** [ActionHandler.py](../cuems-engine/src/cuemsengine/cues/ActionHandler.py) `_handle_fade_in` (line 402):
1. Arm target (existing)
2. Set `target._fade_initial_volume = 0.0` on the cue object (side-channel for run_cue)
3. Go target — `ch.go(target, mtc)` launches `go_threaded` in background thread
4. Resolve target's OSC endpoint (after arm, before go returns):
   - **AudioCue** → AudioPlayer's OSC port + `/volmaster`. Port is at `target._osc.remote_port` (set during arm by `PlayerHandler.new_audio_output()` → `PlayerClient(remote_port=port)`).
   - **VideoCue** → port 7000 + `/videocomposer/layer/{layer_id}/opacity`. Layer ID in `target._layer_ids` after arm.
5. Send NNG `NodeOperation(COMMAND, UPDATE, target="fadeengine", data={start_fade...})` via `CUE_HANDLER.communications_thread`
6. **Unit conversion:** `start_value=0.0, end_value=float(target.master_vol) / 100.0` (UI uses 0-100%, OSC expects 0.0-1.0 — see run_cue.py line 140)

**Volume conflict with run_cue:** `run_audioCue` (run_cue.py line 134-150) sets `/volmaster` once at cue start. Use a **side-channel on the cue object** to override:
- ActionHandler sets `target._fade_initial_volume = 0.0` before calling `ch.go()`
- Modify `run_audioCue` to check `getattr(cue, '_fade_initial_volume', None)` — if set, use that value instead of `cue.master_vol / 100.0` for the initial `/volmaster` write, then delete the attribute
- This avoids changing the `@singledispatch` signature or `go_threaded` parameters
- **Race condition:** `go_threaded` runs `run_audioCue` in a background thread. ActionHandler sends NNG to FadeEngine after `ch.go()` returns. Since `run_audioCue` sets vol=0 first and FadeEngine starts on the next MTC quarter frame (up to 5ms later), the initial vol=0 is set before FadeEngine's first tick. Acceptable ordering.

**Modify** `_handle_fade_out` (line 417):
1. Resolve start_value: `float(target.master_vol) / 100.0` — the cue's configured volume as 0.0-1.0 gain. This matches what `run_audioCue` initially set. (If a previous fade changed the volume, track the last-known value on the cue: `getattr(target, '_current_volume', float(target.master_vol) / 100.0)`)
2. Resolve port: same as fade_in — `target._osc.remote_port`
3. Send NNG start_fade with `start_value=current_vol, end_value=0.0`
4. On `fade_complete` NNG status from FadeEngine → trigger disarm. **Implementation:** Add `OperationType.STATUS` to `NodeCommunications.set_receive_callbacks()` (currently only registers COMMAND — line 35-37). The new STATUS handler checks `operation.target == "fade_complete"` and calls `CUE_HANDLER.disarm(cue_by_fade_id)`. The fade_id in the status message maps back to the target cue.

**Modify** [CueHandler.py](../cuems-engine/src/cuemsengine/cues/CueHandler.py) pre-arm path (~line 267):
- Add `fade_in` to the pre-arm condition alongside `play`. Currently only `action_type == 'play'` triggers pre-arm of the ActionCue's target at script load. Since `fade_in` also starts playback (from silence), the target should be pre-armed to avoid arm-at-go-time delay:
  ```python
  if isinstance(cue, ActionCue) and cue._action_target_object:
      if cue.action_type in ('play', 'fade_in'):
          self.arm(cue._action_target_object, init)
  ```

**Modify** project load/stop flow:
- In `ControllerEngine.load_project()` and `ControllerEngine.stop_script()`: send `CANCEL_ALL` to FadeEngine via NNG before killing players. This prevents stale fades from sending OSC to dead player ports.

**Modify** [NodesHub.py](../cuems-engine/src/cuemsengine/comms/NodesHub.py):
- No new OperationType needed — reuse `COMMAND` with `target="fadeengine"`. FadeEngine filters on target field.

---

## Phase 7: Crossfade Support — depends on 4, 6

- `START_CROSSFADE` command includes both fade targets in one message
- `FadeRegistry` links paired `ActiveFade` entries
- Uses `CrossfadePair` from curve library for complementary values
- ActionHandler detects when fade_in targets same output as existing fade_out → sends crossfade command

---

## Reuse from VideoComposer

| Component | Source | Strategy |
|-----------|--------|----------|
| MtcReceiver | `cuems-videocomposer/src/mtcreceiver/` | Copy sources, patch for RtMidi 5.x, extend with quarter-frame callback in `decodeQuarterFrame()` |
| ConfigurationManager pattern | `cuems-videocomposer/.../config/ConfigurationManager.h` | Follow pattern, simpler version |
| Application lifecycle | `VideoComposerApplication` | Follow init/run/shutdown pattern |
| liblo OSC patterns | `OSCRemoteControl.cpp` | Same `lo_send`/`lo_address` usage |
| systemd service | `cuems-videocomposer.service` | Follow same structure |

**Note on mtcreceiver fork:** The copy in gradient-motion-engine will diverge from VideoComposer's copy (callback extension). Long-term, consider extracting mtcreceiver to a shared library (libcuemsmtc). For now, the fork is acceptable — the modification is small (adding one callback invocation in two places).

## Phase Dependency Graph

```
Phase 0 (scaffold)
  ├── Phase 1 (curves)     ─┐
  ├── Phase 2 (MTC)        ─┼── all independent, build in parallel
  └── Phase 3 (NNG)        ─┘
           │
       Phase 4 (registry + tick loop)
           │
       Phase 5 (application + systemd)
           │
       Phase 6 (Python changes)  ← 6b NodeEngine filter MUST deploy before FadeEngine runs
           │
       Phase 7 (crossfade)
```

## Known Limitations / Future Work

- **FadeEngine crash mid-fade:** Parameter stays at `last_sent_value` until restart. Future: NodeEngine could detect FadeEngine absence (NNG heartbeat or systemd watchdog) and reset parameters.
- **UI/Editor fade progress:** No mechanism for displaying active fade status in the frontend. Future: FadeEngine could send periodic progress updates on NNG.
- **Completed fades don't replay on MTC rewind:** Acceptable for live performance (rewind is exceptional). Future: keep completed fades in a "cold" list for rewind replay.
- **Multi-node bus pollution:** FadeEngine status messages are broadcast to all NNG peers. Mitigated by target filtering in NodeEngine (Phase 6b) and ControllerEngine. Volume is low (one message per fade completion).
- **Video opacity priority:** If NodeEngine and FadeEngine both send opacity to the same layer simultaneously, last-write-wins via UDP. In practice, NodeEngine sends `/visible` (binary) and FadeEngine sends `/opacity` (continuous) — different parameters. If future need arises, add a priority lock in VideoComposer's OSC handler.
- **AudioPlayer port lifetime:** AudioPlayer ports are random and tied to the player process lifetime. If the AudioPlayer dies mid-fade (crash, disarm by another cue), FadeEngine sends OSC to a dead port (silent UDP failure). The `fade_complete` will still fire when duration elapses, but the fade has no effect. Acceptable — same behavior as any OSC-to-dead-player scenario.
- **Mixer vs FadeEngine:** The UI controls volume via AudioMixer (`/audiomixer/{name}/{ch}`), while fades control volume via AudioPlayer (`/volmaster`). These are independent gain stages — both are applied to the audio output. A fade to 0.0 silences even if mixer is at 1.0. This is correct behavior (fade is an artistic effect, mixer is an operator control).

## Verification

1. **Unit tests:** `cmake --build build --target test` — curves, registry, NNG parsing
2. **Integration test:** Start FadeEngine + libmtcmaster (MTC generator) + OSC listener on loopback → verify values match expected curve at expected MTC positions
3. **Manual test:** Full stack (FadeEngine + VideoComposer + AudioMixer + NodeEngine + Controller) → trigger ActionCue with fade_in → observe smooth volume/opacity transition
4. **Transport test:** Pause MTC mid-fade → verify fade holds. Resume → verify fade continues. Stop → verify fades cancel.
5. **Cancel test:** Project unload while fade is active → verify CANCEL_ALL arrives and fades stop before mixer reset
6. **NNG filter test:** Send fade command, verify NodeEngine does NOT log error, FadeEngine receives and processes
7. **Crossfade test:** Simultaneous fade_out + fade_in on same audio channel → verify no volume dip/overshoot (complementary values sum to ~1.0)
