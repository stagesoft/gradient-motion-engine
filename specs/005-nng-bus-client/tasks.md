# Tasks: Phase 3 — NNG Bus Client, FadeCommand & LockFreeQueue

**Input**: Design documents from `/specs/005-nng-bus-client/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/ ✓, quickstart.md ✓

**Tests**: Included — spec deliverables explicitly require `test_nng_parse.cpp` (US1)
and `test_lockfree_queue.cpp` (US2/US3). US1 follows TDD order (tests before
implementation). US2/US3 write tests against the already-designed contract headers.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on incomplete tasks)
- **[US1/US2/US3]**: Which user story this task belongs to
- All paths are relative to the repository root

---

## Phase 1: Setup (Build System)

**Purpose**: Expand the CMake build system to support the new sources and NNG dependency.
All three files are independent — they can be edited in parallel.

- [ ] T001 [P] Promote NNG from optional to required in `CMakeLists.txt` — move `pkg_check_modules(NNG nng)` inside `if(BUILD_DAEMON)` as `REQUIRED`; add `${NNG_INCLUDE_DIRS}` to `target_include_directories` and `${NNG_LIBRARIES}` to `target_link_libraries` for the `gradient-motiond` target → `CMakeLists.txt`
- [ ] T002 [P] Register `daemon/comms/NngBusClient.cpp` in the daemon source list in `CMakeLists.txt` — add it to `add_executable(gradient-motiond ...)` alongside the existing daemon sources → `CMakeLists.txt`
- [ ] T003 [P] Add `signal/FadeCommand.cpp` to `GME_SOURCES` in `src/CMakeLists.txt` so the library translation unit compiles; add three test targets to `tests/CMakeLists.txt`: `test_nng_parse` and `test_lockfree_queue` linked against `gradient_motion` only (no NNG), and `test_nng_integration` linked against `gradient_motion` + `${NNG_LIBRARIES}` with `${NNG_INCLUDE_DIRS}` on its include path (the integration test exercises the real NNG loopback per G6/SC-001/SC-003/SC-006) → `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Checkpoint**: `cmake -S . -B build` must complete without error before Phase 2.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core headers and config extension that every user story depends on.
T004 and T005 are independent of each other (different files). T006 must precede T007.
T008 is independent of T004–T007.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T004 [P] Create `src/signal/FadeCommand.h` from `specs/005-nng-bus-client/contracts/FadeCommand.h` — copy verbatim: `FadeCommand` struct, `ParseResult` enum, `parseFadeCommand` declaration, `ParseOutcomeAction` enum, and `classifyParseOutcome` declaration; verify it compiles as part of `gradient_motion` → `src/signal/FadeCommand.h`
- [ ] T005 [P] Create `src/signal/LockFreeQueue.h` from `specs/005-nng-bus-client/contracts/LockFreeQueue.h` — copy the full template including inline `push`, `pop`, `size`, `empty` implementations; verify it compiles by including it from `src/signal/signal.cpp` → `src/signal/LockFreeQueue.h`
- [ ] T006 [P] Add `--node-name` flag to `daemon/config/ConfigurationManager.h` — add private `nodeName_` field (`std::string`) and public `getNodeName() const` getter; add `<climits>` and `<unistd.h>` to the include list for `HOST_NAME_MAX` and `gethostname` → `daemon/config/ConfigurationManager.h`
- [ ] T007 Add `--node-name` parsing to `daemon/config/ConfigurationManager.cpp` — add `'n'` case to `getopt_long` with `required_argument`; when `--node-name` is absent, call `gethostname(2)` and store result as default, logging `"node_name defaulted to hostname: <name>"` at info level; return error if hostname retrieval fails → `daemon/config/ConfigurationManager.cpp`
- [ ] T008 [P] Create `daemon/comms/NngBusClient.h` from `specs/005-nng-bus-client/contracts/NngBusClient.h` — copy verbatim; add two members needed for the fallback drain (Phase 5): `std::atomic_flag drain_in_progress_` (initialized `ATOMIC_FLAG_INIT`) and `std::thread fallbackThread_`; add `drainOnce(const std::function<void(FadeCommand&)>&)` declaration → `daemon/comms/NngBusClient.h`

**Checkpoint**: `cmake --build build` must produce `gradient-motiond` binary (no test binaries yet required) and `gradient_motion` static library.

---

## Phase 3: User Story 1 — Fade Commands Received from Controller (Priority: P1) 🎯 MVP

**Goal**: Full JSON → FadeCommand parse pipeline; NNG socket connect, receive, filter, enqueue, and status-send.

**Independent Test**: Run `ctest -R nng_parse` — all 25 parse + dispatch test cases pass without an NNG hub running.

### Tests for User Story 1 (TDD — write tests FIRST; they must FAIL before implementation)

- [ ] T009 [US1] Create `tests/test_nng_parse.cpp` with 25 failing test cases (build must succeed but all `CHECK` assertions must fail or test must crash). **Group A — parseFadeCommand (11 cases)**: all valid frames set `target: "gradientengine"` and the fade-specific kind under `data.command`. Cases: valid `start_fade` round-trip (all 10 fields, `osc_path` key); `TargetMismatch` on `target: "nodeengine"` (and implicitly on any non-`"gradientengine"` value — parameterise two different wrong-target strings to confirm it is not a whitelist of two); `NodeMismatch` on wrong `node_name`; `MalformedJson` on empty bytes; `MissingField` on absent `fade_id`; `TypeError` on `duration_ms` as string; `UnknownCommand` on `data.command: "blink"` (target is correctly `"gradientengine"`; the unknown kind is inside `data`); `cancel_fade` populates only `fade_id`; `cancel_all` produces empty `fade_id`; `start_crossfade` round-trip (all partner_* fields incl. `partner_osc_path`); forward-compat (unknown `data.foo` ignored, `curve_params.newKnob` preserved). **Group B — classifyParseOutcome decision table (14 cases, exhaustive)**: every combination of `ParseResult` (7 values: Ok, TargetMismatch, NodeMismatch, UnknownCommand, MissingField, TypeError, MalformedJson) × `hasFadeId` (false, true); assert the return matches the authoritative table in `FadeCommand.h` — specifically that `MissingField`/`TypeError` return `LogOnly` when `hasFadeId=false` and `LogAndStatus` when `hasFadeId=true`, and every other row returns the same action regardless of `hasFadeId` → `tests/test_nng_parse.cpp`
- [ ] T010 [US1] Verify tests fail: `cmake --build build --target test_nng_parse && ctest --test-dir build -R nng_parse`; confirm all 25 cases fail before proceeding to implementation → (build + run verification step, no new files)

### Implementation for User Story 1

- [ ] T011 [US1] Create `src/signal/FadeCommand.cpp` with internal parse helpers — file-scope anonymous-namespace helpers `getString`, `getInt`, `getFloat`, `getLong` (return bool, output via reference); each helper calls `.contains()` then `.is_*()` to avoid exceptions; must be usable by all per-command parsers below → `src/signal/FadeCommand.cpp`
- [ ] T012 [US1] Implement `parseFadeCommand` top-level dispatch in `src/signal/FadeCommand.cpp` — in order: (1) `MalformedJson` guard; (2) `TargetMismatch` on `envelope["target"] != "gradientengine"` (uniform target for this daemon; any other value drops silently); (3) `data` object presence check; (4) `NodeMismatch` on `data["node_name"]` vs `ownNodeName`; (5) `data["command"]` dispatch to `parseStartFade`, `parseCancelFade`, `parseCancelAll`, `parseStartCrossfade`; (6) `UnknownCommand` fallthrough (future non-fade command names on the same target land here) → `src/signal/FadeCommand.cpp`
- [ ] T013 [US1] Implement `parseStartFade` in `src/signal/FadeCommand.cpp` — extract all 10 required fields per data-model.md required-field matrix using the JSON/struct key names verbatim (wire/struct parity — the JSON key is `osc_path`, not `osc_address`); populate `out.fade_id` as the first field (for error attribution on later failures); store absent `curve_params` as `nlohmann::json::object()`; silently skip unknown top-level `data` keys and unknown `curve_params` keys → `src/signal/FadeCommand.cpp`
- [ ] T014 [US1] Implement `parseCancelFade`, `parseCancelAll`, `parseStartCrossfade` in `src/signal/FadeCommand.cpp` — `cancel_fade`: require `fade_id`; `cancel_all`: set `type = CANCEL_ALL`, `fade_id = ""`; `start_crossfade`: reuse `parseStartFade` logic then additionally extract `partner_fade_id`, `partner_osc_path`, `partner_start_value`, `partner_end_value` (wire key names identical to struct field names); shared fields (`duration_ms`, `curve_type`, `curve_params`, `osc_host`, `osc_port`, `start_mtc_ms`) are read once and stored in the primary fields → `src/signal/FadeCommand.cpp`
- [ ] T014a [US1] Implement `classifyParseOutcome(ParseResult, bool hasFadeId) noexcept` in `src/signal/FadeCommand.cpp` — a pure `switch` over `ParseResult` returning `ParseOutcomeAction` per the authoritative table in `FadeCommand.h`; no I/O, no state, no allocation; must not throw; verify the 14 decision-table tests from T009 Group B all pass immediately after this task → `src/signal/FadeCommand.cpp`
- [ ] T015 [P] [US1] Create `daemon/comms/NngBusClient.cpp` with constructor, `start()`, and `stop()` — constructor: initialise `senderId_ = "gradientengine_" + nodeName_`; `start()`: `nng_bus0_open`, set `NNG_OPT_RECONNMINT=1000` and `NNG_OPT_RECONNMAXT=30000` before dial, `nng_dial(NNG_FLAG_NONBLOCK)`, set `running_ = true`, launch `recvThread_`; `stop()`: `running_ = false`, `nng_close`, `recvThread_.join()` — note: nng_close unblocks any blocking nng_recv call → `daemon/comms/NngBusClient.cpp`
- [ ] T016 [US1] Implement `NngBusClient::recvLoop()` in `daemon/comms/NngBusClient.cpp` — `while (running_)` loop calling `nng_recv(NNG_FLAG_ALLOC)`; on `NNG_EAGAIN`/`NNG_ETIMEDOUT` continue; on `nng_recv` error after `!running_` break; parse bytes with `nlohmann::json::parse(..., nullptr, false)`; call `parseFadeCommand(env, nodeName_, cmd)` to get `ParseResult r`, then call `classifyParseOutcome(r, !cmd.fade_id.empty())` and `switch` on the returned `ParseOutcomeAction`: `Enqueue` → `queue_.push(std::move(cmd))` (log `GME_LOG_WARNING` if push returns false — drop-oldest); `DropSilent` → no-op; `LogOnly` → `GME_LOG_WARNING(describeParseResult(r))`; `LogAndStatus` → `GME_LOG_WARNING` + `sendStatus(StatusKind::FadeError, cmd.fade_id, "parse_error")`; always `nng_free(buf, sz)`. No side effects may be performed outside the four branches — this is the only dispatch path → `daemon/comms/NngBusClient.cpp`
- [ ] T017 [US1] Implement `NngBusClient::sendStatus()` and `isConnected()` in `daemon/comms/NngBusClient.cpp` — build the JSON envelope per spec clarification Q2: `{type:"status", action:"update", sender:senderId_, target:"gradientengine", data:{event:"fade_complete"|"fade_error", fade_id:..., node_name:nodeName_[, reason:...]}}` using `nlohmann::json` (map `StatusKind::FadeComplete`/`FadeError` to `data.event` string literals; `target` is always `"gradientengine"`); omit `data.reason` when the `reason` argument is empty; call `nng_send` (blocking); log error on failure but do not throw; `isConnected()` returns `connected_.load()` → `daemon/comms/NngBusClient.cpp`
- [ ] T017a [P] [US1] Create `tests/test_nng_integration.cpp` exercising the real NNG loopback (G6 integration coverage for SC-001, SC-003, SC-006). Every producer frame sent by the test MUST set `target: "gradientengine"`, `data.command: "start_fade"`, and `data.node_name` matching the `NngBusClient` under test — no other target values are valid. **SC-001 (latency)**: open a `pair1` or `bus0` listener on `inproc://gme-test`, connect an `NngBusClient` to it, send a well-formed `start_fade` frame, measure wall-clock from `nng_send` return to `queue_.pop()` success; assert ≤ 5 ms (99th percentile over 100 iterations). **SC-003 (drop rate)**: drive 100 cmd/s × 10 s = 1000 frames through the socket on one producer thread; count `queue_.push` drop returns via an injected counter; assert drops ≤ 1. **SC-006 (reconnect)**: start the client, stop the remote listener, restart it on the same URL after 10 s; assert a fresh command sent ≥ 5 s after the restart is received within 30 s of the initial disconnect. Also include one **round-trip outbound check**: after a successful parse, call `client.sendStatus(StatusKind::FadeComplete, "fade_test1")` and assert the receiving end observes a frame with `target: "gradientengine"`, `data.event: "fade_complete"`, `data.fade_id: "fade_test1"` (validates the outbound envelope schema end-to-end). Use `std::chrono::steady_clock` for all timing; guard each case behind `#ifdef GME_ENABLE_INTEGRATION_TESTS` so CI can gate slow tests → `tests/test_nng_integration.cpp`
- [ ] T018 [US1] Verify US1 tests pass: `cmake --build build && ctest --test-dir build -R "nng_parse|nng_integration" --output-on-failure`; all 25 parse/dispatch cases must pass; all 3 integration cases (SC-001, SC-003, SC-006) must pass

**Checkpoint**: User Story 1 is complete. `parseFadeCommand` correctly handles all 6 acceptance scenarios from spec.md including node filtering and crossfade.

---

## Phase 4: User Story 2 — Thread-Safe Command Hand-off (Priority: P1)

**Goal**: `LockFreeQueue<FadeCommand, 64>` is verified correct under single-thread, drop-oldest, and concurrent SPSC access. TSan-clean.

**Independent Test**: Run `ctest -R lockfree_queue` under both the normal build and the TSan build — all cases pass with zero detected races.

*(Note: `LockFreeQueue.h` was already implemented in T005 from the contract template. Phase 4 is purely verification via tests.)*

- [ ] T019 [US2] Create `tests/test_lockfree_queue.cpp` with single-thread FIFO correctness test — instantiate `LockFreeQueue<int, 8>`, push integers 0–999, pop 1000 items, assert pop returns `true` each time and values arrive in FIFO order; also assert `empty()` returns true after draining → `tests/test_lockfree_queue.cpp`
- [ ] T020 [US2] Add drop-oldest overflow test to `tests/test_lockfree_queue.cpp` — capacity is `N`, usable slots are `N-1`; push `N+5` items with sequence values 0…N+4; drain the queue; verify only the last `N-1` items remain (oldest were dropped); verify `push` returned `false` exactly 6 times (1 reserved slot + 5 overflows) → `tests/test_lockfree_queue.cpp`
- [ ] T021 [US2] Add SPSC stress test to `tests/test_lockfree_queue.cpp` — producer `std::thread` pushes `FakeCmd{seq}` structs 0…9999; consumer `std::thread` pops until it has received 10000 - drops items (push returned false); verify no seq number appears twice (duplicates indicate a race); verify final count = pushed count - drop count; join both threads → `tests/test_lockfree_queue.cpp`
- [ ] T022 [US2] Run tests under normal and TSan builds: `cmake --build build && ctest --test-dir build -R lockfree_queue --output-on-failure`; then `cmake --build build-tsan --target test_lockfree_queue && ctest --test-dir build-tsan -R lockfree_queue --output-on-failure`; both must pass with zero TSan reports

**Checkpoint**: User Story 2 is complete. `LockFreeQueue` is proven correct and race-free under concurrent producer/consumer access.

---

## Phase 5: User Story 3 — Commands Processed When MTC Is Stopped (Priority: P2)

**Goal**: A 100 ms fallback drain timer fires independently of MTC ticks, ensuring `CANCEL_ALL` and other commands are consumed within 200 ms when the tick thread is idle. Consumer sites are serialised via `drain_in_progress_` to prevent concurrent pops.

**Independent Test**: Timing test in `test_lockfree_queue.cpp` directly pushes a `CANCEL_ALL` command to the queue while simulating a 100 ms drain timer, and asserts consumption within 200 ms.

- [ ] T023 [US3] Add `drainOnce()` implementation to `daemon/comms/NngBusClient.cpp` — tries to `test_and_set(drain_in_progress_, memory_order_acquire)`; if flag was already set, return immediately (another site is draining); otherwise pop all available items from `queue_` calling `drainCallback_` for each; `clear(drain_in_progress_, memory_order_release)` on exit; drainCallback_ is a `std::function<void(FadeCommand&)>` stored as a member, set by `start()` → `daemon/comms/NngBusClient.cpp`
- [ ] T024 [US3] Add `drainCallback_` member and update `start()` signature in `daemon/comms/NngBusClient.h` — change `start(const std::string& url)` to `start(const std::string& url, std::function<void(FadeCommand&)> drainCallback)`; add `std::function<void(FadeCommand&)> drainCallback_` private member; this is the callback invoked by both the fallback drain timer and (Phase 4) the MTC tick thread → `daemon/comms/NngBusClient.h`
- [ ] T025 [US3] Implement fallback drain timer thread in `daemon/comms/NngBusClient.cpp` — `fallbackThread_` body: while `running_`, sleep via `std::condition_variable::wait_for(lk, 100ms)`; on wake: call `drainOnce()`; thread is launched in `start()` and joined in `stop()` (`stop()` must notify the condition variable before join to unblock the 100 ms wait) → `daemon/comms/NngBusClient.cpp`
- [ ] T026 [US3] Add US3 timing test to `tests/test_lockfree_queue.cpp` — simulate the drain pattern without NNG: create a `LockFreeQueue<FadeCommand, 64>`; spin a background thread that calls `drainOnce`-equivalent (poll queue every 100 ms); push a `CANCEL_ALL` `FadeCommand` directly; assert the drain callback fires within 200 ms (`std::chrono::steady_clock` based assertion); assert `out.type == FadeCommand::Type::CANCEL_ALL` → `tests/test_lockfree_queue.cpp`
- [ ] T027 [US3] Re-run full test suite to confirm US3 changes do not break US1/US2: `cmake --build build && ctest --test-dir build --output-on-failure`; all four test targets (`test_curves`, `test_mtc_tick_source`, `test_nng_parse`, `test_lockfree_queue`) must pass

**Checkpoint**: All three user stories are independently functional and tested. The command ingest pipeline is complete. Phase 4 wiring task: instantiate `NngBusClient` in `GradientEngineApplication::initialize()` (first task of Phase 4 — deferred per research Decision 7).

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Doxygen compliance (constitution Principle VI), final test sweep, and documentation build gate.

- [ ] T028 [P] Audit Doxygen docstrings on `src/signal/FadeCommand.h` — verify `parseFadeCommand` and `classifyParseOutcome` free functions each have: brief, all parameters, long description, `@throws None`, `@par Example`; verify `FadeCommand::Type`, `ParseResult`, and `ParseOutcomeAction` enum values all carry `///` comments; confirm the `classifyParseOutcome` decision-table appears verbatim in the class docstring (it is the authoritative contract for the 14 tests in T009) → `src/signal/FadeCommand.h`
- [ ] T029 [P] Audit Doxygen docstrings on `src/signal/LockFreeQueue.h` — verify class docstring has: brief, `@tparam T`, `@tparam N`; verify `push`, `pop`, `size`, `empty`, `capacity` each have brief + params + return + `@throws None` + `@par Example`; no inline comments needed for private members → `src/signal/LockFreeQueue.h`
- [ ] T030 [P] Audit Doxygen docstrings on `daemon/comms/NngBusClient.h` — verify `start()` signature matches Phase 5 update (two-parameter with `drainCallback`); verify `drainOnce` and `sendStatus` additions from Phase 5 are documented with the same standard; update file-level `@file` comment to mention the fallback drain → `daemon/comms/NngBusClient.h`
- [ ] T031 Build Doxygen and confirm zero warnings: `cmake --build build --target docs`; `DOXYGEN_WARN_AS_ERROR=YES` is already set in `CMakeLists.txt` — the build must succeed with no warnings → (Doxygen build step)
- [ ] T032 Final full test sweep across both build types: `cmake --build build && ctest --test-dir build --output-on-failure` (must include `test_nng_integration` with `-DGME_ENABLE_INTEGRATION_TESTS=ON`); `cmake --build build-tsan && ctest --test-dir build-tsan --output-on-failure`; zero failures, zero TSan reports → (final verification step)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 completion — blocks all user stories
- **US1 (Phase 3)**: Depends on Phase 2 (needs `FadeCommand.h`, `NngBusClient.h`, `ConfigurationManager` node-name) — TDD: T009–T010 before T011–T017
- **US2 (Phase 4)**: Depends on Phase 2 (needs `LockFreeQueue.h`) — independent of US1
- **US3 (Phase 5)**: Depends on Phase 2 + US1 completion (needs `NngBusClient.cpp` from T015–T017) — also needs US2 tests passing for the timing test pattern
- **Polish (Phase 6)**: Depends on all phases complete

### User Story Dependencies

| Story | Depends on | Can start independently after |
|-------|-----------|-------------------------------|
| US1 (P1) | Phase 2 | Phase 2 complete |
| US2 (P1) | Phase 2 (T004–T005 only) | T005 complete |
| US3 (P2) | Phase 2 + US1 (T015–T017 for NngBusClient.cpp) | US1 Phase 3 complete |

### Within Phase 3 (US1)

```
T009 → T010    (write 25 tests, verify all fail)
T010 → T011    (begin implementation only after tests fail)
T011 → T012    (helpers before dispatch)
T012 → T013    (dispatch before start_fade impl)
T013 → T014    (start_fade before cancel/crossfade variants)
T014 → T014a   (classifyParseOutcome — pure decision table, no deps beyond ParseResult values)
T015 ‖ T014a   (NngBusClient constructor/start/stop independent of parser; same file — sequential)
T015 → T016    (recvLoop consumes parseFadeCommand + classifyParseOutcome)
T016 → T017    (sendStatus called by recvLoop's LogAndStatus branch)
T017 → T017a   (integration test needs full client)
T017a → T018   (verify all 25 parse/dispatch + 3 integration cases)
```

### Within Phase 4 (US2)

T019 → T020 → T021 (same file, sequential) → T022 (verification)

### Within Phase 5 (US3)

T023 → T024 → T025 → T026 → T027

---

## Parallel Opportunities

### Phase 1 (all three tasks in parallel)

```
Task: "T001 — NNG CMake required + link in CMakeLists.txt"
Task: "T002 — daemon comms source in CMakeLists.txt"   (same file as T001 — serialize)
Task: "T003 — src/CMakeLists.txt FadeCommand.cpp + tests/CMakeLists.txt"
```

T001 and T002 both touch `CMakeLists.txt` — do them together in one edit session.

### Phase 2 (four parallel tasks)

```
Task: "T004 — src/signal/FadeCommand.h"
Task: "T005 — src/signal/LockFreeQueue.h"
Task: "T006 + T007 — daemon/config/ConfigurationManager.h + .cpp"
Task: "T008 — daemon/comms/NngBusClient.h"
```

### Phase 3 Phase 4 overlap (US1 and US2 are both P1)

Once Phase 2 is complete, US1 and US2 can proceed concurrently:
- Developer A: T009–T018 (US1 parse pipeline)
- Developer B: T019–T022 (US2 queue tests — T005 is already done)

### Phase 6 (three tasks in parallel)

```
Task: "T028 — FadeCommand.h docstring audit"
Task: "T029 — LockFreeQueue.h docstring audit"
Task: "T030 — NngBusClient.h docstring audit"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001–T003)
2. Complete Phase 2: Foundational (T004–T008)
3. Complete Phase 3: US1 TDD cycle (T009–T018)
4. **STOP and VALIDATE**: `ctest -R nng_parse` — all 11 parse scenarios pass
5. The parse pipeline is complete and integration-ready for Phase 4's `FadeRegistry`

### Incremental Delivery

1. Phase 1 + 2 → build system and headers ready
2. Phase 3 (US1) → command ingest works, parseFadeCommand + NngBusClient complete → MVP
3. Phase 4 (US2) → queue safety verified, TSan clean
4. Phase 5 (US3) → fallback drain ensures CANCEL_ALL is never silently lost
5. Phase 6 → Doxygen gate passes, full clean build

### Solo Developer Order (Sequential)

T001 → T002 → T003 → T004 → T005 → T006 → T007 → T008 → T009 → T010 → T011 → T012 → T013 → T014 → T014a → T015 → T016 → T017 → T017a → T018 → T019 → T020 → T021 → T022 → T023 → T024 → T025 → T026 → T027 → T028 → T029 → T030 → T031 → T032

---

## Notes

- `[P]` tasks modify different files — safe to implement in parallel or in any order
- `FadeCommand.h` and `LockFreeQueue.h` are copied from `specs/005-nng-bus-client/contracts/` — the contracts are the authoritative specification; any deviation must be justified
- The `nng_socket` forward-declaration in `NngBusClient.h` (`struct nng_socket_s`) is a placeholder to avoid pulling `<nng/nng.h>` into library-visible headers; in `NngBusClient.cpp` use the real `nng_socket` value type directly
- TSan build directory (`build-tsan/`) is already configured in the repo — use it for T022 and T032 without additional setup
- Phase 4 (GradientEngineApplication wiring) will be the FIRST task of the next feature branch — the `drainCallback` parameter of `start()` will be wired to `FadeRegistry::apply` at that point
- Commit after each checkpoint (end of Phase 1, 2, 3, 4, 5, 6) to preserve clean rollback points
