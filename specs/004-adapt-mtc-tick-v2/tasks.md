# Tasks: Adapt MtcTickSource to mtcreceiver v2.0.0

**Input**: Design documents from `/specs/004-adapt-mtc-tick-v2/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/ ✓, quickstart.md ✓

**Tests**: Included — spec FR-005, FR-006, FR-007, FR-008, FR-010 explicitly mandate test rewrites and a concurrency regression test.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1–US4)

---

## Phase 1: Setup

**Purpose**: Confirm the workspace is hygienic before any submodule or source changes. The build environment itself is already configured from spec 003; no tooling installation is needed.

- [ ] T001 [P] Verify workspace hygiene: `git status` shows no unexpected modifications inside `mtcreceiver/`, `src/time/`, or `tests/`; `git submodule status` confirms the mtcreceiver submodule is initialised (no `-` prefix)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Put the v2.0.0 headers and test build flag in place so downstream user-story tasks can compile and exercise the new API. T002 and T003 touch different files and have no ordering dependency between them.

**⚠️ CRITICAL**: No user story implementation work can begin until this phase is complete.

- [ ] T002 Bump `mtcreceiver/` submodule to commit `59fc76e` (v2.0.0): run `git -C mtcreceiver fetch origin`, `git -C mtcreceiver checkout 59fc76e`, then `git add mtcreceiver` from the repo root (FR-001)
- [ ] T003 [P] Add `target_compile_definitions(test_mtc_tick_source PRIVATE MTCRECV_TESTING)` to `tests/CMakeLists.txt` — enables `invokeTickForTesting`, `SkipPortOpenTag`, and the decoder-reset helpers on the test target only, not on the library or daemon (FR-007)

**Checkpoint**: Submodule pointer updated, test target configured for the testing helpers. Compilation of `src/` will break (expected) until US2 adapter lands; tests will break until US4 rewrite lands.

---

## Phase 3: User Story 1 — Submodule at v2.0.0 (Priority: P1) 🎯 MVP

**Goal**: Confirm the submodule pin is correct and the new v2.0.0 header is in place.

**Independent Test**: `git -C mtcreceiver rev-parse HEAD` returns `59fc76e`. The new `setTickCallback` symbol must be visible in `mtcreceiver/mtcreceiver.h`.

> Note: Full clean-build verification (US1 Acceptance Scenario 2) requires US2 to be complete. T004 validates the pin itself; T022 (Polish) validates the full build.

- [ ] T004 [US1] Verify `git -C mtcreceiver rev-parse HEAD` returns `59fc76e` and that `mtcreceiver/mtcreceiver.h` contains `setTickCallback` (grep confirms the v2.0.0 header is in place)

**Checkpoint**: Submodule pin confirmed at v2.0.0.

---

## Phase 4: User Story 2 — setTickCallback Adapter (Priority: P1)

**Goal**: `MtcTickSource::setTickCallback()` registers/deregisters callbacks through the new thread-safe `MtcReceiver::setTickCallback()` API, adapting `void(long)` to `void(long, bool)` by ignoring `isCompleteFrame`.

**Independent Test**: After this phase the project must compile. The concurrency test in US4 (T019) will exercise US2 Acceptance Scenario 3 (multi-thread registration) — because `MTCRECV_TESTING` is already enabled by T003, US2's independent test is runnable as soon as T019 lands.

- [ ] T005 [US2] Rewrite `MtcTickSource::setTickCallback()` in `src/time/MtcTickSource.cpp`: replace `MtcReceiver::onQuarterFrame = std::move(cb)` with the v2.0.0 adapter — truthy `cb` → register adapter lambda `[cb=std::move(cb)](long ms, bool) { cb(ms); }` via `MtcReceiver::setTickCallback()`; falsy `cb` → call `MtcReceiver::setTickCallback({})` (FR-002, FR-003)
- [ ] T006 [P] [US2] Update `setTickCallback` Doxygen docstring in `src/time/MtcTickSource.h`: remove "must call before start()" restriction (now thread-safe per v2.0.0), state that `isCompleteFrame` is ignored, correct QF rate from "200 Hz" to "100 Hz at 25 fps" in `@brief` and `@example` (Principle VI / FR-002)

**Checkpoint**: Project now compiles against v2.0.0 (US1 full-build acceptance satisfied). `setTickCallback` is wired to the new API.

---

## Phase 5: User Story 3 — Destructor Guarantee (Priority: P2)

**Goal**: Destroying a `MtcTickSource` immediately stops all callbacks, blocking until any in-flight invocation completes.

**Independent Test**: After this phase: wrap `MtcTickSource` lifetime in a block, fire `invokeTickForTesting` from outside the block — consumer callback count must not increase post-destruction (verified by T024 in Polish).

- [ ] T007 [US3] Replace `~MtcTickSource() = default;` with `~MtcTickSource();` declaration in `src/time/MtcTickSource.h` (FR-004)
- [ ] T008 [US3] Add explicit destructor definition in `src/time/MtcTickSource.cpp`: body calls `MtcReceiver::setTickCallback({})` unconditionally before any other teardown (FR-004)
- [ ] T009 [P] [US3] Add Doxygen `~MtcTickSource()` docstring in `src/time/MtcTickSource.h` documenting: deregisters callback via `setTickCallback({})`, blocks until in-flight invocation returns, guarantees no-call-after-dtor (Principle VI / SC-004)

**Checkpoint**: `MtcTickSource` lifecycle is safe for multi-threaded teardown.

---

## Phase 6: User Story 4 — Tests Compile and Pass (Priority: P2)

**Goal**: All `test_mtc_tick_source` scenarios compile and pass against the v2.0.0 testing helpers, with zero references to the removed `MtcReceiver::onQuarterFrame`, Scenario B repurposed as the single-fire-per-QF contract, and the multi-thread registration scenario (T019) in place.

**Independent Test**: `ctest --test-dir build -R test_mtc_tick_source --output-on-failure` passes all scenarios within 30 s (SC-002).

> Prerequisite: T003 (MTCRECV_TESTING flag) already landed in Foundational, so the testing helpers are visible.

### Rewrite test harness infrastructure

- [ ] T010 [US4] Rewrite `resetMtcState()` in `tests/test_mtc_tick_source.cpp`: delete all `MtcReceiver::onQuarterFrame = nullptr` assignments (member no longer exists), replace with `MtcReceiver::setTickCallback({})` and `MtcReceiver::resetStaticStateForTesting()`, and reset `MtcReceiver::mtcHead` and `MtcReceiver::isTimecodeRunning` atomics (FR-005)

### Rewrite Scenario A

- [ ] T011 [US4] Rewrite `testScenarioA_callbackInvocation` in `tests/test_mtc_tick_source.cpp`: replace `MtcReceiver::onQuarterFrame(1234L)` with 8 `invokeTickForTesting` calls using the 7×`false`+1×`true` pattern (FR-005, FR-008); assert callback received the last delivered ms value

### Rewrite Scenario B (repurposed to single-fire contract)

- [ ] T012 [US4] Rewrite `testScenarioB_bothSitesFire` into `testScenarioB_singleFirePerQF` in `tests/test_mtc_tick_source.cpp`: call `invokeTickForTesting` N times with mixed flag values (at least one `false` and one `true`); assert consumer callback count equals exactly N (one invocation per call, no double-fires) and that the `true`-flag call also produces exactly one invocation (FR-005)

### Rewrite Scenario C

- [ ] T013 [US4] Rewrite `testScenarioC_nullCallbackSafe` in `tests/test_mtc_tick_source.cpp`: with no callback registered (or registered as `{}`), call `invokeTickForTesting(999L, false)` and assert no crash. The v2.0.0 API null-checks internally, so the test simply invokes the helper; no consumer-side guard is required (FR-005, FR-008)

### Rewrite Scenario D

- [ ] T014 [P] [US4] Update `testScenarioD_queryBeforeStart` in `tests/test_mtc_tick_source.cpp`: remove any `onQuarterFrame` reference; logic (getMtcMs()==0, isRunning()==false before start()) is unchanged — verify it still compiles and passes (FR-005)

### Rewrite Scenario E

- [ ] T015 [P] [US4] Update `testScenarioE_invalidPortReturnsError` in `tests/test_mtc_tick_source.cpp`: remove any `onQuarterFrame` reference from setup; `start("__no_such_port__")` logic is unchanged — verify it still compiles and passes (FR-005)

### Rewrite synthetic-stream test

- [ ] T016 [US4] Rewrite `testSyntheticMtcStream` in `tests/test_mtc_tick_source.cpp`: replace every `MtcReceiver::onQuarterFrame(ms)` call with `invokeTickForTesting(ms, isCompleteFrame)` using the 7×`false`+1×`true` pattern repeating every 8 QFs; drive exactly 1200 calls at 10 ms intervals (kExpectedMs = 12000); assert `count == 1200` and `|getMtcMs() − 12000| ≤ 1`; remove the multi-paragraph stale 60 s comment block (FR-005, FR-006, FR-008, SC-003)

### Add latency instrumentation (SC-006)

- [ ] T017 [US4] Extend `testSyntheticMtcStream` in `tests/test_mtc_tick_source.cpp` (or add a sibling `testSyntheticMtcStream_latency`) to measure per-call dispatch latency: record `std::chrono::steady_clock::now()` immediately before each `invokeTickForTesting` call and immediately after the consumer callback returns (capture `now()` inside the registered callback); collect 1200 deltas into a `std::vector<long long>`; after the loop, sort and compute the p99; assert p99 < 1 000 000 ns (1 ms). Report min, median, p99, and max in the PASS/FAIL message (SC-006, FR-010-adjacent)

### Add no-call-after-dtor regression test (SC-004)

- [ ] T018 [US4] Add `testScenarioF_noCallAfterDestruction` in `tests/test_mtc_tick_source.cpp`: register a callback that increments an atomic counter; destroy the `MtcTickSource` by letting it go out of scope; snapshot the counter value; call `invokeTickForTesting(1L, false)` and `invokeTickForTesting(2L, true)`; assert counter is unchanged after both calls (SC-004)

### Add multi-thread registration regression test (US2 AS-3, FR-010)

- [ ] T019 [US4] Add `testScenarioG_concurrentRegistration` in `tests/test_mtc_tick_source.cpp` covering **US2 Acceptance Scenario 3** and **FR-010**: main thread constructs a `MtcTickSource` and fires `invokeTickForTesting` in a tight loop (≥10 000 iterations); a spawned worker thread alternates `src.setTickCallback(cb)` and `src.setTickCallback({})` on a schedule at least 100 times; after both threads join, assert no crash and that exactly one of two outcomes holds at each tick: either no callback fires or exactly one callback fires (never two). Designed to be clean under TSan (see T023) — the adapter itself serialises on mtcreceiver's internal mutex (FR-010, US2 AS-3)

**Checkpoint**: All test scenarios (A, B, C, D, E, F, G, synthetic stream) compile and pass under `ctest`.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final build validation, sanitiser runs, docstring audit, and acceptance evidence for all six success criteria.

- [ ] T020 [P] Clean rebuild and zero-warnings check: `cmake --build build --clean-first 2>&1 | tee build.log; grep -iE 'error:|warning:.*mtcreceiver' build.log` — confirm zero errors and zero mtcreceiver-related warnings (SC-001)
- [ ] T021 [P] Run full test suite: `ctest --test-dir build --output-on-failure` and confirm the `test_mtc_tick_source` binary alone completes within 30 s wall-clock (SC-002)
- [ ] T022 Verify SC-003 acceptance: inspect `testSyntheticMtcStream` output for exact count 1200 and final MTC head within ±1 ms of 12000 ms (SC-003)
- [ ] T023 Run TSan build and verify zero data-race reports (SC-005): add a dedicated build directory `build-tsan` configured with `-DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1"` and `-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread`; rebuild `test_mtc_tick_source`; run it with `TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1"`; grep output for `WARNING: ThreadSanitizer`. Pass = zero warnings in `MtcTickSource` and `MtcReceiver` frames. T019 (concurrent registration scenario) is the primary exerciser (SC-005)
- [ ] T024 Verify SC-004 (no-call-after-dtor) under the same TSan build from T023 — confirm `testScenarioF_noCallAfterDestruction` passes with zero sanitiser reports; document any AddressSanitizer follow-up if use-after-free is suspected (SC-004)
- [ ] T025 Verify SC-006 (p99 latency < 1 ms): review T017 output; if p99 exceeds the budget, record measured values in quickstart.md and open a follow-up issue rather than masking the regression (SC-006)
- [ ] T026 [P] Audit `src/time/MtcTickSource.h` and `src/time/MtcTickSource.cpp` for any remaining v1 references: grep for `onQuarterFrame`, `"200 Hz"`, `"before start()"`, `"must be called before start"` — each must be absent or updated. Also confirm the "One-instance constraint" docstring block is preserved in `MtcTickSource.h` (FR-009 carry-forward) (Principle VI, FR-009)
- [ ] T027 [P] Verify `git -C mtcreceiver rev-parse HEAD` still returns `59fc76e` after all changes (guard against accidental submodule drift during the feature branch)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)** and **Foundational (Phase 2)**: Independent. T001 (workspace hygiene) and T002/T003 (submodule + CMake flag) can run concurrently. No ordering dependency between them.
- **US1 (Phase 3)**: Depends on Phase 2 (T002 submodule pin) — verify-only, no code changes.
- **US2 (Phase 4)**: Depends on Phase 2 (T002). US2's independent test runs once US4 T019 exists, but US2 implementation (T005–T006) is blocked only by the submodule bump.
- **US3 (Phase 5)**: Depends on Phase 4 (explicit destructor touches same files as the adapter).
- **US4 (Phase 6)**: Depends on Phase 2 (MTCRECV_TESTING flag, T003) + Phase 4 (adapter) + Phase 5 (destructor).
- **Polish (Phase 7)**: Depends on Phases 3–6 complete. T023 (TSan) explicitly depends on T019 (concurrent registration scenario).

### User Story Dependencies

- **US1 (P1)**: Depends only on Phase 2 (submodule bump).
- **US2 (P1)**: Depends only on Phase 2. The *implementation* (T005–T006) is independent of US4; the *acceptance scenario 3* verification lives in T019 (US4 phase) and depends on T005.
- **US3 (P2)**: Depends on US2 implementation (same source files; destructor calls the adapter-registered API).
- **US4 (P2)**: Depends on US2 + US3 (tests exercise both adapter and destructor) + Phase 2 T003 (testing flag).

### Within Phase 6 (US4)

- T010 (resetMtcState) MUST precede T011–T019 (all scenarios call it).
- T011–T016 are independent of each other (different functions in the same file) and can be drafted in parallel.
- T017 depends on T016 (instruments the same test) OR creates a sibling test — either way it runs after T016.
- T018 (Scenario F) and T019 (Scenario G) are independent of each other and of T011–T016; both can run in parallel with scenario rewrites.

---

## Parallel Opportunities

### Phase 1 + Phase 2
```
T001 [P]          — workspace hygiene   (no file writes)
T002 [sequential] — mtcreceiver/        (submodule pointer)
T003 [P]          — tests/CMakeLists.txt (compile definition)
```
All three can start concurrently.

### Phase 4 (US2)
```
T005 [sequential] — src/time/MtcTickSource.cpp (adapter body)
T006 [P]          — src/time/MtcTickSource.h   (docstring only)
```

### Phase 5 (US3)
```
T007 [sequential] — src/time/MtcTickSource.h   (destructor declaration)
T008 [sequential] — src/time/MtcTickSource.cpp (destructor body; after T007)
T009 [P]          — src/time/MtcTickSource.h   (docstring only; after T007)
```

### Phase 6 (US4) — after T010
```
T011 [US4] testScenarioA                    ─┐
T012 [US4] testScenarioB (single-fire)      ─┤  All in tests/test_mtc_tick_source.cpp
T013 [US4] testScenarioC                    ─┤  but different functions → can be
T014 [P][US4] testScenarioD                 ─┤  drafted in parallel and merged
T015 [P][US4] testScenarioE                 ─┤
T016 [US4] testSyntheticMtcStream           ─┤
T018 [US4] testScenarioF (no-call-after-dtor)─┤
T019 [US4] testScenarioG (concurrent reg.)  ─┘
T017 [US4] latency instrumentation — after T016
```

### Phase 7 (Polish)
```
T020 [P] clean rebuild
T021 [P] ctest full suite
T026 [P] v1-reference audit
T027 [P] submodule HEAD re-check
```

T022, T023, T024, T025 run sequentially after T021 (they interpret/consume test output and sanitiser results).

---

## Implementation Strategy

### MVP (US1 + US2 only)

1. Phase 1 (T001) + Phase 2 (T002, T003) — workspace + foundations
2. Phase 3 (T004) — US1 pin verified
3. Phase 4 (T005, T006) — US2 adapter + docstring: project compiles
4. **STOP and VALIDATE**: Build succeeds, `MtcTickSource::setTickCallback` routes through the new API

### Incremental Delivery

1. Foundation (T001–T004) → submodule correct, test build configured, pin verified
2. US2 (T005–T006) → project compiles, adapter wired
3. US3 (T007–T009) → destructor safe
4. US4 (T010–T019) → tests green, including concurrent-registration and no-call-after-dtor scenarios
5. Polish (T020–T027) → SC-001 through SC-006 evidenced end-to-end

---

## Notes

- **[P] tasks** = touch different files or are purely additive (docstrings), no shared-state conflicts.
- **[Story] label** maps each task to the user story whose acceptance scenarios it fulfils. Note: Phase 6 tasks T018 (Scenario F) and T019 (Scenario G) are labelled `[US4]` because they live in the test file that US4 owns, but their *subject* is US3's destructor contract and US2's multi-thread registration respectively. The phase/label split is a traceability artefact, not an architectural coupling.
- T002 (submodule bump) intentionally breaks library compilation until T005 lands — keep T002 and T005 on the same PR, or land them in adjacent commits on the feature branch so `main` never sees a broken tip.
- `MTCRECV_TESTING` must remain scoped to `test_mtc_tick_source` — never add it to the library or daemon targets (FR-007).
- T017 (latency) and T023 (TSan) are the two measurements that bind SC-005 and SC-006; if either fails, fix the underlying issue rather than relax the bound. If a genuine regression is identified as out-of-scope for this feature, document it in spec.md and open a follow-up feature.
