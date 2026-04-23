# Tasks: Phase 2 — MTC Tick Source

**Input**: Design documents from `/specs/003-mtc-tick-source/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/MtcTickSource.h, quickstart.md

**Tests**: Included — the spec defines unit test scenarios (quickstart.md Scenarios A–E) and the quickstart smoke test.

**Organization**: Tasks are grouped by user story. Upstream submodule work is treated as a high-priority foundational phase so the pin is advanced before library implementation begins.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Submodule Tracking Fix)

**Purpose**: Fix the stale `master→main` branch tracking in the `mtcreceiver` submodule so that subsequent upstream work targets the correct branch. No code changes — only git metadata.

- [x] T001 Fix `mtcreceiver` submodule branch tracking: inside `mtcreceiver/`, run `git branch -m master main && git fetch origin && git branch -u origin/main main && git remote set-head origin -a`. Verify with `git branch -vv` that local `main` tracks `origin/main` at commit `63ce3de`
- [x] T002 Verify submodule pin is unchanged after tracking fix: run `git submodule status` from repo root and confirm `mtcreceiver` still points to `63ce3de`

---

## Phase 2: Foundational — Upstream `mtcreceiver` Extension (Independent Task)

**Purpose**: Extend `stagesoft/mtcreceiver` with the `onQuarterFrame` callback and `portIndex` constructor parameter. These changes are committed to the upstream repo on branch `feat/quarter-frame-callback`, merged to `main`, and the submodule pin advanced. This is an **independent upstream task** — changes go into the original `stagesoft/mtcreceiver` repository.

**CRITICAL**: No user story implementation can begin until the submodule pin is advanced to include these changes.

### Upstream code changes (committed to `stagesoft/mtcreceiver`)

- [x] T003 Add `static std::function<void(long)> onQuarterFrame` public member declaration in `mtcreceiver/mtcreceiver.h`. Add `#include <functional>` if not already present. Initialize to `nullptr` in the definition file
- [x] T004 Append `unsigned int portIndex` as the **last** parameter of the `MtcReceiver` constructor in `mtcreceiver/mtcreceiver.h`, with default value `0`. New signature: `MtcReceiver(RtMidi::Api api = MTCRECV_DEFAULT_API, const std::string& clientName = "Cuems Mtc Receiver", unsigned int queueSizeLimit = 100, unsigned int portIndex = 0)`. Placing it last preserves positional backward compatibility for existing callers
- [x] T005 Define `std::function<void(long)> MtcReceiver::onQuarterFrame = nullptr;` static member initialization in `mtcreceiver/mtcreceiver.cpp`
- [x] T006 Modify `MtcReceiver` constructor in `mtcreceiver/mtcreceiver.cpp` to use the `portIndex` parameter instead of hardcoded `0` in the `RtMidiIn::openPort()` call (~line 73)
- [x] T007 Add `if (onQuarterFrame) onQuarterFrame(mtcHead.load());` after the per-quarter running update `mtcHead.store()` in `decodeQuarterFrame()` in `mtcreceiver/mtcreceiver.cpp` (~line 281, Site 1)
- [x] T008 Add `if (onQuarterFrame) onQuarterFrame(mtcHead.load());` after the per-complete-frame `mtcHead.store()` inside the `if (complete)` block in `decodeQuarterFrame()` in `mtcreceiver/mtcreceiver.cpp` (~line 299, Site 2)
- [x] T009 Verify that `decodeFullFrame()` in `mtcreceiver/mtcreceiver.cpp` does NOT call `onQuarterFrame` — this is intentional (SysEx seek, not a running tick)

### Upstream commit and submodule pin advance

- [x] T010 Commit upstream changes to `stagesoft/mtcreceiver` on branch `feat/quarter-frame-callback`, then merge to `main` and push
- [x] T011 Advance the `mtcreceiver` submodule pin in `gradient-motion-engine` to the new `main` HEAD commit: run `cd mtcreceiver && git checkout main && git pull origin main && cd .. && git add mtcreceiver`. Verify with `git submodule status` that the pin is at the new commit (not `63ce3de`)

**Checkpoint**: Submodule is pinned to the new upstream commit with `onQuarterFrame` and `portIndex` available. User story implementation can begin.

---

## Phase 3: User Story 1 — Engine Receives Timecode-Driven Ticks (Priority: P1) MVP

**Goal**: Implement `MtcTickSource` with `setTickCallback()` and `start()` so the gradient engine receives a callback on every MTC quarter frame.

**Independent Test**: Construct `MtcTickSource`, register a callback, call `start()` with a valid MIDI port, feed synthetic MTC messages, verify callback fires at expected rate with correct ms values.

### Implementation for User Story 1

- [x] T012 [P] [US1] Create `src/time/MtcTickSource.h` — declare `enum class MtcStartError { kOk, kNoPortsAvailable, kPortNotFound }` and `gme::time::MtcTickSource` class per contract (`specs/003-mtc-tick-source/contracts/MtcTickSource.h`). Include: constructor/destructor (default), delete copy/move, `setTickCallback(std::function<void(long)>)`, `MtcStartError start(const std::string&)`, `getMtcMs() const`, `isRunning() const`. Private member: `std::unique_ptr<MtcReceiver> receiver_`. Add Doxygen docstrings with usage examples per Constitution VI, and one-instance-per-process warning (FR-008)
- [x] T013 [P] [US1] Create `src/time/MtcTickSource.cpp` — implement all four public methods: (1) `setTickCallback()`: assign callback to `MtcReceiver::onQuarterFrame` (FR-006). (2) `start()`: call `MtcReceiver::setNetworkMode(true)` (FR-002), scan RtMidi ports via temporary `RtMidiIn` probe for substring match on `midiPort`, return `MtcStartError::kNoPortsAvailable` if no ports or `MtcStartError::kPortNotFound` if no match (FR-001), construct `MtcReceiver` with resolved `portIndex` as last param via `std::make_unique<MtcReceiver>(MTCRECV_DEFAULT_API, "Cuems Mtc Receiver", 100, portIndex)`, return `MtcStartError::kOk` (FR-001, FR-007). (3) `getMtcMs()`: return `MtcReceiver::mtcHead.load()` (FR-004). (4) `isRunning()`: return `MtcReceiver::isTimecodeRunning.load()` (FR-005)
- [x] T014 [US1] Add `time/MtcTickSource.cpp` to `GME_SOURCES` in `src/CMakeLists.txt`. Ensure `${CMAKE_SOURCE_DIR}` is in `target_include_directories` for `gradient_motion` (needed for `#include "mtcreceiver/mtcreceiver.h"`)
- [x] T015 [US1] Build `gradient_motion` library target: run `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target gradient_motion` and verify compilation succeeds with no errors or warnings

### Tests for User Story 1

- [x] T016 [US1] Create `tests/test_mtc_tick_source.cpp` with test scenarios from quickstart.md: Scenario A (callback invocation — directly call `MtcReceiver::onQuarterFrame(1234)` and verify registered callback receives `1234`), Scenario C (null callback safety — invoke callback guard path with no callback set, verify no crash), Scenario D (`getMtcMs()` before `start()` returns `0`, `isRunning()` returns `false`), Scenario E (`start("__no_such_port__")` returns `MtcStartError::kPortNotFound`)
- [x] T017 [US1] Add `test_mtc_tick_source` test executable in `tests/CMakeLists.txt`: link against `gradient_motion`, `mtcreceiver`, and `rtmidi`. Register with CTest via `add_test()`
- [x] T018 [US1] Build and run tests: `cmake --build build --target test_mtc_tick_source && cd build && ctest -R test_mtc_tick_source -V`. Verify all scenarios pass

**Checkpoint**: `MtcTickSource` compiles, `setTickCallback()` + `start()` work, tick callback fires, error paths validated. US1 independently testable.

---

## Phase 4: User Story 2 — Timecode Position is Queryable at Any Time (Priority: P2)

**Goal**: `getMtcMs()` and `isRunning()` return correct atomic values at any time, from any thread.

**Independent Test**: Call `getMtcMs()` and `isRunning()` before `start()`, during synthetic MTC, and after MTC stops. Verify correct values.

### Verification for User Story 2

> **Note**: `getMtcMs()` and `isRunning()` are implemented in T013 as part of the complete `MtcTickSource` class. This phase is verification-only.

- [x] T019 [US2] Add test cases to `tests/test_mtc_tick_source.cpp`: verify `getMtcMs()` returns `0` before `start()` (Scenario D — may already exist from T016), verify `isRunning()` returns `false` before `start()`

**Checkpoint**: Position and running state are queryable. US2 independently testable alongside US1.

---

## Phase 5: User Story 3 — Network Mode is Enabled (Priority: P2)

**Goal**: `start()` unconditionally calls `MtcReceiver::setNetworkMode(true)` before opening the MIDI port.

**Independent Test**: Code inspection or integration test confirms `setNetworkMode(true)` is called prior to `MtcReceiver` construction inside `start()`.

### Implementation for User Story 3

- [x] T020 [US3] Verify (code review) that `MtcTickSource::start()` in `src/time/MtcTickSource.cpp` calls `MtcReceiver::setNetworkMode(true)` as the first statement before any `RtMidiIn` probe or `MtcReceiver` construction (FR-002). This should already be implemented in T013 — this task is a verification checkpoint only

**Checkpoint**: Network mode confirmed. US3 verified.

---

## Phase 6: User Story 4 — Callback Fires from Correct Decode Site (Priority: P3)

**Goal**: The `onQuarterFrame` callback fires from inside `decodeQuarterFrame()` after BOTH `mtcHead` update sites, not from `midiCallback()` or `decodeFullFrame()`.

**Independent Test**: Code review or unit test verifying callback fires at both update sites and NOT from SysEx path.

### Implementation for User Story 4

- [x] T021 [US4] Add Scenario B test to `tests/test_mtc_tick_source.cpp`: simulate both decode sites firing by setting `MtcReceiver::onQuarterFrame` to a counting lambda, then calling the callback twice with different ms values. Verify callback fires exactly twice with expected values
- [x] T022 [US4] Verify (code review) that upstream `mtcreceiver/mtcreceiver.cpp` has the callback at both sites inside `decodeQuarterFrame()` (T007, T008) and NOT inside `decodeFullFrame()` (T009). This is a verification checkpoint for the upstream work done in Phase 2

**Checkpoint**: Callback correctness verified at both decode sites. All user stories complete.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Regression testing, documentation, and final validation

- [x] T023 [P] Run existing Phase 1 tests to confirm no regression: `cmake --build build && cd build && ctest -V`. Verify `test_curves` passes unchanged (SC-004)
- [x] T024 [P] Run quickstart.md submodule smoke check: `git submodule status` (confirm new commit, not `63ce3de`) and `git -C mtcreceiver log --oneline -3` (confirm top commit includes `onQuarterFrame` addition)
- [x] T025 Create synthetic MTC stream integration test in `tests/test_mtc_tick_source.cpp`: generate 60 seconds of quarter-frame ticks at 25 fps (1200 callbacks total, at 200 Hz) by invoking `MtcReceiver::onQuarterFrame` in a timed loop with correct ms increments (250/25 = 10 ms per quarter frame). Verify: (a) callback count equals exactly 1200 with no missed or duplicated calls (SC-001), (b) final `getMtcMs()` value is within ±5 ms of expected 60000 ms (SC-002)
- [x] T026 Verify Doxygen-compatible docstrings with usage examples are present on all public methods of `MtcTickSource` in `src/time/MtcTickSource.h` (Constitution VI — Documentation Standards)
- [x] T027 Final full build and test pass: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build && cd build && ctest -V`. All tests green

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational — Upstream)**: Depends on Phase 1 (tracking fix) — **BLOCKS all user stories**
- **Phase 3 (US1)**: Depends on Phase 2 (submodule pin advanced with `onQuarterFrame`)
- **Phase 4 (US2)**: Depends on Phase 3 (MtcTickSource class exists); or can be implemented alongside US1 in T013
- **Phase 5 (US3)**: Depends on Phase 3 (verification of `start()` implementation)
- **Phase 6 (US4)**: Depends on Phase 2 (upstream code) + Phase 3 (test infrastructure)
- **Phase 7 (Polish)**: Depends on all user stories complete

### User Story Dependencies

- **US1 (P1)**: Requires Phase 2 complete. Core deliverable — all other stories depend on this class existing
- **US2 (P2)**: `getMtcMs()` and `isRunning()` are implemented as part of the same class as US1; can be coded in parallel with US1 (same files)
- **US3 (P2)**: Verification-only — depends on US1 implementation being correct
- **US4 (P3)**: Verification of upstream work (Phase 2) + additional test case

### Within Each User Story

- Header before implementation (T012 before T013)
- Implementation before CMake integration (T013 before T014)
- CMake before build verification (T014 before T015)
- Build before tests (T015 before T016–T018)

### Parallel Opportunities

- T003, T004 can run in parallel (different sections of `mtcreceiver.h`)
- T005, T006 can run in parallel (different sections of `mtcreceiver.cpp`)
- T007, T008 can run in parallel (different sites in `decodeQuarterFrame()`)
- T012, T013 can run in parallel (header vs implementation file)
- T023, T024 can run in parallel (independent validation checks)

---

## Parallel Example: Phase 2 Upstream Work

```text
# Parallel: header changes
Task T003: "Add onQuarterFrame static member to mtcreceiver/mtcreceiver.h"
Task T004: "Add portIndex constructor param to mtcreceiver/mtcreceiver.h"

# Parallel: implementation changes (after header)
Task T005: "Define onQuarterFrame static init in mtcreceiver/mtcreceiver.cpp"
Task T006: "Use portIndex in constructor in mtcreceiver/mtcreceiver.cpp"

# Parallel: callback sites (after static init)
Task T007: "Add callback at Site 1 in decodeQuarterFrame()"
Task T008: "Add callback at Site 2 in decodeQuarterFrame()"
```

## Parallel Example: User Story 1

```text
# Parallel: header and implementation
Task T012: "Create MtcTickSource.h"
Task T013: "Create MtcTickSource.cpp"

# Sequential: build integration (after T012, T013)
Task T014: "Update src/CMakeLists.txt"
Task T015: "Build verification"

# Sequential: test creation (after T015)
Task T016: "Create test_mtc_tick_source.cpp"
Task T017: "Update tests/CMakeLists.txt"
Task T018: "Run tests"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Submodule tracking fix (T001–T002)
2. Complete Phase 2: Upstream `mtcreceiver` extension (T003–T011) — **independent upstream task**
3. Complete Phase 3: User Story 1 — tick callback (T012–T018)
4. **STOP and VALIDATE**: Run all tests, verify callback fires correctly

### Incremental Delivery

1. Phase 1 + Phase 2 → Upstream ready, submodule pinned
2. Add US1 (Phase 3) → Test independently → Core MTC ticking works (MVP!)
3. Add US2 (Phase 4) → Queryable position verified (verification-only, implementation in T013)
4. Add US3 (Phase 5) → Network mode verified
5. Add US4 (Phase 6) → Decode site correctness verified
6. Phase 7 → Full regression pass, synthetic stream integration test, documentation check

---

## Notes

- The upstream `mtcreceiver` changes (Phase 2) are an **independent task** committed to the `stagesoft/mtcreceiver` repository, not just to the local submodule checkout
- The submodule tracking fix (Phase 1) is **high priority** — it must happen before any upstream branch work
- US2 (`getMtcMs`, `isRunning`) and US3 (network mode) are naturally implemented as part of US1's `MtcTickSource` class — their separate phases serve as verification checkpoints
- `setNetworkMode(true)` already exists at commit `63ce3de` — no upstream addition needed for this method
- No free-running timer anywhere — all tick delivery is driven by incoming MIDI messages (FR-008)
