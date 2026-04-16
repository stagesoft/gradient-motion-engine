# Research: Phase 2 — MTC Tick Source

**Branch**: `003-mtc-tick-source` | **Date**: 2026-04-16

---

## Decision 1: `onQuarterFrame` callback — exact placement in `decodeQuarterFrame()`

**Decision**: Call `onQuarterFrame` after BOTH `mtcHead.store()` sites inside `decodeQuarterFrame()`:

1. After the per-quarter running update (line 281 of current `mtcreceiver.cpp`):
   ```cpp
   mtcHead.store(mtcHead.load() + static_cast<long int>(250 / curFrame.getFps()));
   // → call onQuarterFrame here (site 1)
   ```
2. After the per-complete-frame decode (line 299, inside `if (complete)`):
   ```cpp
   mtcHead.store(curFrame.toMilliseconds());
   // → call onQuarterFrame here (site 2)
   ```

The callback is NOT called from `decodeFullFrame()` — that SysEx path is for seek/locate operations (position jumps), not for the running quarter-frame tick stream. The spec explicitly constrains the callback to `decodeQuarterFrame()`.

**Rationale**: The consumer (GradientEngine tick loop) needs the callback to fire at the per-quarter cadence (200 Hz at 25 fps) with the freshest available `mtcHead` value. Calling after site 1 handles the running-head case; calling after site 2 handles the full-frame-decoded case (which overwrites the running estimate). Not calling from `decodeFullFrame()` avoids spurious ticks on SysEx seeks.

**Alternatives considered**:
- Single call after the full switch/complete block: rejected — the running update (site 1) would not trigger the callback when the complete-frame path fires (site 2 replaces site 1's value), meaning the callback value might be briefly stale.
- Call from `midiCallback()`: rejected — spec constraint and correctness (callback would fire before `decodeQuarterFrame()` commits the new `mtcHead` value).

---

## Decision 2: `onQuarterFrame` member type and null-safety

**Decision**: Declare as `static std::function<void(long)> onQuarterFrame` in `MtcReceiver`. Before invoking, guard with `if (onQuarterFrame)` to avoid undefined behavior when no callback is registered. Initialize to `nullptr` in the definition.

**Rationale**: `std::function` has a bool conversion operator that returns false for null/empty callables. A null check is idiomatic, zero-overhead for the common case (callback is set at startup and never changes), and avoids a segfault if `MtcReceiver` is instantiated without `MtcTickSource` setting a callback (e.g., in tests using `MtcReceiver` directly).

**Alternatives considered**:
- Raw function pointer `static void(*onQuarterFrame)(long)`: simpler but limits callers to free functions/static lambdas; `std::function` allows capturing lambdas needed for `MtcTickSource` to route to its internal state.
- `std::atomic<std::function<void(long)>>`: not required — callback is set once at init time before `start()` (clarification Q3 answer); no atomic swap needed.

---

## Decision 3: Port selection in `MtcTickSource::start(portName)`

**Decision**: `MtcTickSource::start(const std::string& portName)` uses the `portName` parameter to find the matching RtMidi port index via `RtMidiIn::getPortCount()` / `getPortName(i)`. If no matching port is found, throw `std::runtime_error`. The upstream `MtcReceiver` constructor is extended to accept an optional `unsigned int portIndex` parameter (defaulting to 0) so that `MtcTickSource` can pass the resolved index.

The `MtcReceiver` extension to the constructor signature:
```cpp
MtcReceiver(unsigned int portIndex = 0,
            RtMidi::Api api = MTCRECV_DEFAULT_API,
            const std::string& clientName = "Cuems Mtc Receiver",
            unsigned int queueSizeLimit = 100);
```
Port scanning is done in `MtcTickSource::start()` before constructing `MtcReceiver`, using a temporary `RtMidiIn` probe instance.

**Rationale**: Hardcoding port 0 would make `--midi-port NAME` CLI argument a no-op. Scanning by name before construction is the RtMidi idiomatic pattern (used in many RtMidi examples). The `portIndex` default of 0 preserves backward compatibility with all existing `MtcReceiver` users (e.g., VideoComposer's own usage).

**Alternatives considered**:
- Keep port 0 hardcoded for Phase 2: rejected — the `start(portName)` interface in the spec makes port selection an explicit requirement; hardcoding would require a follow-up upstream change anyway.
- Modify `MtcReceiver` to open by name directly: rejected — would require full RtMidi port-scan loop inside MtcReceiver, mixing responsibilities; better to have `MtcTickSource` own the scan.

---

## Decision 4: `MtcReceiver` upstream modification strategy

**Decision**: The `onQuarterFrame` callback addition and constructor `portIndex` parameter are committed to a new branch `feat/quarter-frame-callback` on `stagesoft/mtcreceiver`, then merged to `main`. The `gradient-motion-engine` submodule pin is advanced to the resulting `main` HEAD commit.

The submodule's local branch tracking is first updated (fetch + re-point to `origin/main`) as a prerequisite task. This is a pure tracking fix with no code change — the pinned commit (`63ce3de`) does not move at this step.

**Rationale**: The clarification session resolved this explicitly. `63ce3de` is already `main` HEAD after the upstream rename. Keeping modifications upstream ensures VideoComposer (another `mtcreceiver` consumer) can adopt the same callback mechanism without duplicating the patch.

**Alternatives considered**:
- Local-only submodule modification (no upstream push): acceptable as fallback but creates divergence from upstream and makes future submodule updates require a rebase. Rejected as primary strategy.

---

## Decision 5: CMake linkage — `gradient_motion` vs `mtcreceiver`

**Decision**: `MtcTickSource.h/.cpp` in `src/time/` uses `mtcreceiver/mtcreceiver.h`. The include path is provided by adding `${CMAKE_SOURCE_DIR}` to `gradient_motion`'s include directories (already present for the daemon). Link resolution happens at the daemon's final link step (daemon already links both `gradient_motion` and `mtcreceiver`). No `target_link_libraries(gradient_motion ... mtcreceiver)` needed — the STATIC library archives are merged at executable link time.

For tests that exercise `MtcTickSource` directly, `tests/CMakeLists.txt` links the test executable against both `gradient_motion` and `mtcreceiver`.

**Rationale**: Adding `mtcreceiver` as a PUBLIC link dependency of `gradient_motion` would force every consumer of `libgradient_motion` to also link RtMidi — violating Constitution Principle III (embeddable without daemon dependencies). Deferring RtMidi linkage to the executable keeps the library boundary clean.

**Alternatives considered**:
- Separate `gradient_motion_time` sub-library: over-engineered for Phase 2; revisit if `gme::time` needs to be independently distributed.
- Conditional compilation (`BUILD_WITH_MTC` CMake option): reasonable but premature; add only if an embedder actually requests RtMidi-free builds.

---

## Decision 6: `MtcTickSource` ownership of `MtcReceiver` instance

**Decision**: `MtcTickSource` holds a `std::unique_ptr<MtcReceiver>` created in `start()` and destroyed in the destructor (or a `stop()` call). This is null before `start()` — calling `getMtcMs()` or `isRunning()` before `start()` reads `MtcReceiver::mtcHead.load()` directly (static member, safe, returns 0 before any MTC arrives).

**Rationale**: `MtcReceiver`'s constructor immediately opens the MIDI port and spawns the checker thread. Delaying construction until `start()` is called gives the caller time to call `setTickCallback()` and `setNetworkMode()` before any MIDI processing begins. The `unique_ptr` ensures RAII cleanup on `MtcTickSource` destruction.

---

## Resolution of `setNetworkMode` — confirmed existing

`MtcReceiver::setNetworkMode(bool enabled)` **already exists** at commit `63ce3de` (introduced in the `feat/rtmidi-api-rename` branch, `37b9788 Add configurable timeouts for network transport (rtpmidid)`). No upstream addition needed for this method. `MtcTickSource::start()` simply calls it before constructing the `MtcReceiver` instance.
