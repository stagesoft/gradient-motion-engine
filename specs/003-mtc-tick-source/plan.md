# Implementation Plan: Phase 2 — MTC Tick Source

**Branch**: `003-mtc-tick-source` | **Date**: 2026-04-16 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `specs/003-mtc-tick-source/spec.md`

---

## Summary

Implement `gme::time::MtcTickSource` — a thin adapter over the `MtcReceiver` MIDI timecode decoder that exposes a quarter-frame tick callback, a queryable MTC position, and a running-state predicate. This requires two coordinated workstreams: (1) extending the upstream `stagesoft/mtcreceiver` submodule with a static `onQuarterFrame` callback and an optional port-index constructor parameter, and (2) implementing `MtcTickSource.h/.cpp` in `src/time/` as part of `libgradient_motion`. Network mode (`setNetworkMode(true)`) is called unconditionally in `start()` — it already exists at the pinned commit `63ce3de`.

---

## Technical Context

**Language/Version**: C++17 (GCC, `-Wall -O3 -pthread`)  
**Primary Dependencies**:
- `librtmidi-dev` >= 5.0 — MIDI/MTC reception (via `mtcreceiver` submodule)
- `libasound2-dev` — ALSA MIDI backend
- C++ standard library only: `<functional>`, `<atomic>`, `<memory>`, `<string>`, `<stdexcept>`

**Storage**: N/A  
**Testing**: `cmake --build build --target test` (CTest), `tests/test_mtc_tick_source.cpp`  
**Target Platform**: Linux (ALSA MIDI backend, systemd deployment)  
**Project Type**: Static library module (`gme::time`) + upstream submodule modification  
**Performance Goals**: Zero heap allocation per tick in steady state; callback latency < 1 ms (Constitution §IV)  
**Constraints**: No free-running timer; no dynamic allocation in tick hot path; callback fires from MIDI thread (must be lock-free)  
**Scale/Scope**: Single process, single MIDI port, 200–240 Hz tick rate

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| **I. Deterministic Evaluation** | ✅ Pass | `MtcTickSource` passes raw `mtcHead` ms values to consumers; no wall-clock dependency in the tick path. |
| **II. Modular Architecture** | ✅ Pass | `gme::time` module is independently compilable. No circular deps with other `gme::*` modules. |
| **III. Library-First** | ✅ Pass (justified) | `MtcTickSource` lives in `libgradient_motion/src/time/`. RtMidi linkage is resolved at the daemon executable link step — the library archive itself does not carry RtMidi as a PUBLIC link dependency. See Complexity Tracking. |
| **IV. Real-Time Safety** | ✅ Pass | The callback is a `std::function` invocation — no allocation in steady state (callback pointer is set once at init). `getMtcMs()` and `isRunning()` are atomic loads. |
| **V. Protocol-Agnostic Core** | ✅ Pass | `gme::time` does not reference OSC, NNG, or any transport protocol. It emits `long` ms values. |
| **VI. Documentation Standards** | ✅ Pass | All public methods of `MtcTickSource` documented per contract (see `contracts/MtcTickSource.h`). Doxygen-compatible docstrings required in implementation headers. |

---

## Project Structure

### Documentation (this feature)

```text
specs/003-mtc-tick-source/
├── plan.md              ← This file
├── spec.md              ← Feature specification
├── research.md          ← Phase 0 decisions
├── data-model.md        ← Entity definitions
├── quickstart.md        ← Smoke test scenarios
├── contracts/
│   └── MtcTickSource.h  ← Interface contract
└── tasks.md             ← Phase 2 output (/speckit.tasks — not yet generated)
```

### Source Code Changes

#### A. Upstream: `mtcreceiver/` submodule (`stagesoft/mtcreceiver`)

These changes are committed to a new branch `feat/quarter-frame-callback` on the upstream repo, merged to `main`, then the submodule pin in `gradient-motion-engine` is advanced.

```text
mtcreceiver/
├── mtcreceiver.h          ← ADD: static std::function<void(long)> onQuarterFrame (public)
│                             MODIFY: constructor signature — add portIndex param (default 0)
└── mtcreceiver.cpp        ← MODIFY: decodeQuarterFrame() — call onQuarterFrame after both
│                                     mtcHead.store() sites (lines ~281 and ~299)
│                             MODIFY: constructor — use portIndex instead of hardcoded 0
└── (no other files changed)
```

**Submodule tracking fix** (prerequisite, no code change):
```bash
# Inside mtcreceiver/ submodule:
git branch -m master main
git fetch origin
git branch -u origin/main main
git remote set-head origin -a
```

#### B. Library: `src/time/` (new files)

```text
src/time/
├── MtcTickSource.h        ← NEW: class gme::time::MtcTickSource declaration + Doxygen
└── MtcTickSource.cpp      ← NEW: implementation
    # Removes placeholder content from time/time.cpp (or keeps as namespace stub)
```

#### C. Build system changes

```text
src/CMakeLists.txt         ← ADD: time/MtcTickSource.cpp to GME_SOURCES
                              ADD: target_include_directories pointing to ${CMAKE_SOURCE_DIR}
                                   (for mtcreceiver/mtcreceiver.h include path)
tests/CMakeLists.txt       ← ADD: test_mtc_tick_source.cpp test executable
                              ADD: link against gradient_motion + mtcreceiver + rtmidi
```

**Root `CMakeLists.txt`**: no changes needed — mtcreceiver is already compiled as a submodule and linked into the daemon.

---

## Complexity Tracking

| Justification | Why Needed | Simpler Alternative Rejected Because |
|---------------|------------|--------------------------------------|
| RtMidi include in `libgradient_motion` | `MtcTickSource` wraps `MtcReceiver` which inherits from `RtMidiIn` | Moving `MtcTickSource` to daemon-only violates Constitution III (Library-First); core time abstraction must be in the library |
| Upstream submodule modification (`stagesoft/mtcreceiver`) | `onQuarterFrame` callback and port-by-index constructor are needed; only option is upstream PR or local divergence | Local-only patch creates permanent divergence from upstream; upstream PR keeps both consumers (VideoComposer, gradient-motion-engine) in sync |

---

## Implementation Notes

### `decodeQuarterFrame()` modification (upstream)

The two `mtcHead.store()` sites and their callback placements:

```cpp
// Site 1: per-quarter running update (~line 281)
mtcHead.store(mtcHead.load() + static_cast<long int>(250 / curFrame.getFps()));
if (onQuarterFrame) onQuarterFrame(mtcHead.load());   // ← ADD

// ... (complete-frame detection logic) ...

// Site 2: per-complete-frame decode (~line 299, inside if(complete))
mtcHead.store(curFrame.toMilliseconds());
if (onQuarterFrame) onQuarterFrame(mtcHead.load());   // ← ADD
```

`decodeFullFrame()` (SysEx seek) does NOT get the callback — it is a position jump, not a running tick. This is intentional.

### `MtcTickSource::start()` implementation sketch

```cpp
void MtcTickSource::start(const std::string& midiPort) {
    // Enable network mode BEFORE constructing MtcReceiver
    MtcReceiver::setNetworkMode(true);

    // Scan available ports for name match
    RtMidiIn probe;
    unsigned int nPorts = probe.getPortCount();
    unsigned int portIndex = UINT_MAX;
    for (unsigned int i = 0; i < nPorts; ++i) {
        if (probe.getPortName(i).find(midiPort) != std::string::npos) {
            portIndex = i;
            break;
        }
    }
    if (portIndex == UINT_MAX) {
        throw std::runtime_error("MtcTickSource: MIDI port not found: " + midiPort);
    }

    // Construct MtcReceiver — opens port, starts checker thread
    receiver_ = std::make_unique<MtcReceiver>(portIndex);
}
```

### CMake include path for `mtcreceiver` headers

Add to `src/CMakeLists.txt`:
```cmake
target_include_directories(gradient_motion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}          # ← for #include "mtcreceiver/mtcreceiver.h"
)
```

(The `${CMAKE_SOURCE_DIR}` entry may already be present from Phase 0 — verify before adding.)
