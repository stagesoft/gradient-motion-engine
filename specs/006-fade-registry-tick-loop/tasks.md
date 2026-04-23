---

description: "Task list for Phase 4 — Fade Registry, Tick Loop & OSC Sender"
---

# Tasks: Phase 4 — Fade Registry, Tick Loop & OSC Sender

**Input**: Design documents from `specs/006-fade-registry-tick-loop/`  
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: Included per **SC-005** (unit coverage) and **SC-007** (integration benchmark) in spec.md.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

Single project: `src/`, `tests/`, `daemon/` at repository root (see plan.md).

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Wire build system for new library sources, liblo, and Phase 4 tests.

- [x] T001 Link `liblo` into `gradient_motion` and replace stub sources with `src/osc/OscSender.cpp`, `src/engine/FadeRegistry.cpp`, `src/engine/GradientEngine.cpp` in `src/CMakeLists.txt` (see `specs/006-fade-registry-tick-loop/quickstart.md`)
- [x] T002 Add `test_fade_registry` (label `unit`) and `test_fade_registry_bench` (label `integration`) to `tests/CMakeLists.txt` per `specs/006-fade-registry-tick-loop/quickstart.md`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Shared types, OSC wrapper, fade data shape, and NNG status worker — **required before any user story can be completed end-to-end.**

**⚠️ CRITICAL**: No user story phase should be considered done until this phase is complete.

- [x] T003 Move `StatusKind` from `daemon/comms/NngBusClient.h` into a new `src/signal/StatusEmitRequest.h` and define `StatusEmitRequest` alongside it; keep a using-alias in `daemon/comms/NngBusClient.h` for existing call sites (avoid `libgradient_motion` depending on daemon headers; reconciles with `specs/006-fade-registry-tick-loop/data-model.md` — see analysis finding I2)
- [x] T004 Extend `daemon/comms/NngBusClient.h` and `daemon/comms/NngBusClient.cpp` with bounded `LockFreeQueue<StatusEmitRequest, 64>` (or capacity constant), **status worker thread** that pops and calls `sendStatus`, **drop-oldest on overflow** with warning log (FR-007, SC-002), `stop()` joining the worker, and **drain-serialisation helpers** for the MTC tick path (try-acquire / release pairing with existing `drain_in_progress_`, per `specs/006-fade-registry-tick-loop/research.md` Decision 1 and `specs/006-fade-registry-tick-loop/contracts/gradient-engine-api.md`)
- [x] T005 [P] Define `ActiveFade` per `specs/006-fade-registry-tick-loop/data-model.md` (including `crossfade_partner` always `nullptr` in Phase 4, `consecutive_osc_failures`, pre-built `lo_address`, `std::unique_ptr<gme::gradient::Curve>`) in `src/engine/ActiveFade.h`
- [x] T006 [P] Implement `gme::osc::sendFloat` and `gme::osc::makeAddress` in `src/osc/OscSender.h` and `src/osc/OscSender.cpp` per `specs/006-fade-registry-tick-loop/contracts/osc-sender-api.md`
- [x] T007 Declare `FadeRegistry` in `src/engine/FadeRegistry.h` with `kOscFailureThreshold`, dual indexes (`fades_`, `osc_index_`), `apply(FadeCommand&)`, `addFade`, `cancelFade`, `cancelAll`, `tick`, constructor deps (`MtcTickSource`, status queue, direct status callback) per `specs/006-fade-registry-tick-loop/data-model.md` and `specs/006-fade-registry-tick-loop/contracts/fade-registry-api.md`
- [x] T008 Declare `GradientEngine` + `GradientEngineConfig` in `src/engine/GradientEngine.h` per `specs/006-fade-registry-tick-loop/contracts/gradient-engine-api.md`

**Checkpoint**: Foundation ready — user story implementation can begin.

---

## Phase 3: User Story 1 — MTC-Driven Fade Execution (Priority: P1) 🎯 MVP

**Goal**: Quarter-frame tick evaluates each active fade, sends one UDP OSC float per fade per tick, completes at `end_value`, removes finished fades (OSC path only for this story’s core demo).

**Independent Test**: Mock or drive `FadeRegistry::tick(mtc_ms)` with a synthetic timeline and an OSC listener; values match the curve within **SC-001** (±0.005 at `t = 0.5` for sigmoid); final message is `end_value`; a completed fade does not “replay” after rewind (spec acceptance scenario 3).

### Tests for User Story 1

> **NOTE**: Write these tests **first**; they must **FAIL** until implementation lands.

- [x] T009 [P] [US1] Add failing unit tests in `tests/test_fade_registry.cpp` for SC-001 (sigmoid at `t ≈ 0.5`), `duration_ms == 0` immediate completion, completion removal + final `end_value` OSC, rewind below `start_mtc_ms` yielding `start_value` for an **active** fade (FR-001), and FR-016 `start_mtc_ms == -1` sentinel resolution via `MtcTickSource::getMtcMs()`

### Implementation for User Story 1

- [x] T010 [US1] Implement `FadeRegistry::addFade` for `FadeCommand::Type::START_FADE` in `src/engine/FadeRegistry.cpp`: `CurveFactory::createCurve` on tick thread (FR-015), resolve `start_mtc_ms == -1` via `MtcTickSource::getMtcMs()` (FR-016), build `lo_address`, handle `START_CROSSFADE` by log-and-drop only (spec assumptions). **Note**: FR-014 supersede is implemented in T017 (US2); US1 demos MUST use unique OSC targets.
- [x] T011 [US1] Implement `FadeRegistry::tick` core path in `src/engine/FadeRegistry.cpp`: `t` clamp + `duration_ms == 0 ⇒ t = 1.0` (contract), evaluate curve, `OscSender::sendFloat`, update `last_sent_value`, mark `completed` when `t >= 1.0`, send final `end_value`, remove completed entries from `fades_` and `osc_index_` — **defer** `FadeComplete` / `FadeError` queue pushes to User Story 2 (Phase 4) and **defer** OSC failure threshold handling to User Story 4 (Phase 6)
- [x] T012 [US1] Implement `FadeRegistry::apply` command dispatch in `src/engine/FadeRegistry.cpp` for commands consumed from the tick/drain path (at minimum `START_FADE` for this phase)
- [x] T013 [US1] Implement `GradientEngine::initialize`, `shutdown`, and private `onTick` in `src/engine/GradientEngine.cpp`: own `MtcTickSource`, `LockFreeQueue<FadeCommand, 64>`, `LockFreeQueue<StatusEmitRequest, 64>`, `FadeRegistry`, `NngBusClient`; register MTC callback; `onTick` acquires drain lock, drains command queue → `apply`, calls `tick`, releases lock (`specs/006-fade-registry-tick-loop/contracts/gradient-engine-api.md`, `specs/006-fade-registry-tick-loop/research.md` Decision 7)
- [x] T014 [US1] Integrate `gme::engine::GradientEngine` into `daemon/GradientEngineApplication.h` and `daemon/GradientEngineApplication.cpp`: build `GradientEngineConfig` from `daemon/config/ConfigurationManager`, call `initialize`/`shutdown` in application lifecycle, pass `FadeRegistry::apply` into `NngBusClient::start` drain callback

**Checkpoint**: US1 tests pass; daemon runs engine with live MTC + NNG ingress (status events not yet required for US1 OSC-only checks).

---

## Phase 4: User Story 2 — Fade Completion Status Notification (Priority: P1)

**Goal**: When `t >= 1.0`, enqueue `fade_complete` for the status worker; tick thread never calls NNG send (FR-006b, FR-005); supersede / parse-time errors use queue vs direct `sendStatus` per research.md Decision 1.

**Independent Test**: Short fade, advance MTC past end, assert a `fade_complete`-equivalent status is observed after the worker drains (SC-003 timing budget); push storm overflows queue → drop-oldest, tick never blocks (SC-002).

### Tests for User Story 2

- [x] T015 [P] [US2] Extend `tests/test_fade_registry.cpp` to assert `FadeComplete` is pushed on completion and consumed by the status worker path (unit-level queue + mock or test hook), with a coarse timing assertion covering SC-003 (≤250 ms wall-clock from completion to emission)
- [x] T015b [P] [US2] Add a test in `tests/test_fade_registry.cpp` (or a new `tests/test_nng_status_queue.cpp`) that saturates the 64-slot SPSC status queue and asserts **drop-oldest** semantics with a warning-log side effect (FR-007, SC-002, SC-005)

### Implementation for User Story 2

- [x] T016 [US2] In `src/engine/FadeRegistry.cpp`, enqueue **`FadeComplete`** on completion (FR-005); enqueue **`FadeError`** for `unknown_curve_type` and `osc_address_failed` from the tick thread via the status SPSC queue; invoke **direct** `statusDirect_` / `sendStatus` for supersede + the same errors when `addFade` runs from the **fallback drain** thread (`specs/006-fade-registry-tick-loop/research.md` Decision 1)
- [x] T017 [US2] Implement FR-014 supersede in `src/engine/FadeRegistry.cpp`: on duplicate OSC key `(host, port, path)`, remove prior fade **without final OSC**, emit `fade_error` / `superseded` for the old `fade_id`, register the new fade; mirror spec edge cases for duplicate `fade_id`

**Checkpoint**: US1 + US2 acceptance paths covered; Controller can observe `fade_complete` over NNG.

---

## Phase 5: User Story 3 — Cancel Fade with Snap or Hold (Priority: P2)

**Goal**: `cancelFade(id, snap_to_end)` and `cancelAll()` behave per FR-008 / FR-009; `CANCEL_FADE` / `CANCEL_ALL` commands applied from drained queue.

**Independent Test**: Mid-fade cancel with snap → one final OSC at `end_value`; snap=false → no extra OSC; `cancelAll` → no final OSC for any fade (spec acceptance).

### Tests for User Story 3

- [x] T018 [P] [US3] Extend `tests/test_fade_registry.cpp` for cancel snap vs hold and `cancelAll()` (SC-005, SC-004)

### Implementation for User Story 3

- [x] T019 [US3] Implement `FadeRegistry::cancelFade`, `FadeRegistry::cancelAll`, and `apply` branches for `CANCEL_FADE` / `CANCEL_ALL` in `src/engine/FadeRegistry.cpp` per `specs/006-fade-registry-tick-loop/contracts/fade-registry-api.md`

**Checkpoint**: Cancellation semantics stable for project stop / operator abort.

---

## Phase 6: User Story 4 — OSC Send Failure Reporting (Priority: P2)

**Goal**: Consecutive `lo_send` failures exceed **5** → enqueue `fade_error` / `osc_send_failed`, remove fade (FR-006a, research.md Decision 5).

**Independent Test**: Target closed UDP port; after threshold, `fade_error` with `osc_send_failed`; transient failures below threshold do not kill the fade (spec acceptance).

### Tests for User Story 4

- [x] T020 [P] [US4] Extend `tests/test_fade_registry.cpp` for OSC failure threshold and recovery (SC-006)

### Implementation for User Story 4

- [x] T021 [US4] Extend `FadeRegistry::tick` in `src/engine/FadeRegistry.cpp` with `consecutive_osc_failures` accounting: increment on non-zero `sendFloat` return, reset on success, enqueue `osc_send_failed` and remove fade at threshold

**Checkpoint**: Failed players surface as `fade_error` instead of hanging the cue lifecycle.

---

## Phase 7: User Story 5 — MTC Pause, Resume, Rewind Behavior (Priority: P2)

**Goal**: No OSC while MTC ticks are absent; on resume, values match curve at clamped `t`; rewind before `start_mtc_ms` yields `start_value` (spec acceptance).

**Independent Test**: Unit tests driving `tick` with synthetic `mtc_ms` timelines — no callback ⇒ no send; resume at `T`; rewind before start.

### Tests for User Story 5

- [x] T022 [P] [US5] Extend `tests/test_fade_registry.cpp` with: (a) a test that calls `tick(mtc_ms)` zero times across a synthetic pause interval and asserts zero OSC sends; (b) a resume test asserting the first post-pause `tick(T)` matches `curve.evaluate(clamp((T - start_mtc_ms) / duration_ms))`; (c) a rewind-before-start test asserting the OSC value equals `start_value` (spec User Story 5 scenarios 1–3)

### Implementation for User Story 5

- [x] T023 [US5] Confirm via the T022 tests that no extra guard is needed in `src/engine/GradientEngine.cpp` or `src/time/MtcTickSource.cpp` (quarter-frame callback only fires on MTC advance). If a guard proves necessary, add it and document the rationale in `src/engine/GradientEngine.h`.

**Checkpoint**: Rehearsal-time transport behaviour matches spec.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Performance gate, documentation, quickstart validation.

- [x] T024 [P] Implement `tests/test_fade_registry_bench.cpp` for SC-007 (50 concurrent fades, 200 Hz tick, p99 tick duration ≤ 2 ms wall-clock, loopback OSC)
- [x] T025 [P] Add Doxygen-compatible docstrings (brief + params + errors + compiling example, per Constitution Principle VI; verified by `cmake --build build --target docs` with Doxygen `WARN_AS_ERROR=YES`) to `src/engine/ActiveFade.h`, `src/engine/FadeRegistry.h`, `src/engine/GradientEngine.h`, `src/osc/OscSender.h`, `src/signal/StatusEmitRequest.h`
- [x] T026 Update `daemon/comms/NngBusClient.h` file-level and public API comments for the status worker + SPSC queue semantics
- [x] T027 Run `specs/006-fade-registry-tick-loop/quickstart.md` validation (`ctest -R test_fade_registry`, unit + integration labels) and fix any gaps

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately.
- **Foundational (Phase 2)**: Depends on Setup — **blocks all user stories**.
- **User Stories (Phases 3–7)**: All depend on Foundational completion. **US1** is the narrative MVP; **US2** extends the same tick path with status emission and should follow US1’s core `tick` + engine wiring. **US3–US5** build on the completed registry/engine (implement after US1–US2 unless tasks are clearly isolated).
- **Polish (Phase 8)**: Depends on desired user stories being complete (at minimum US1–US2 for MVP; US3–US5 + T024 for full Phase 4).

### User Story Dependencies

- **US1 (P1)**: After Foundational — no dependency on other stories.
- **US2 (P1)**: After US1 core OSC path (`tick` + engine drain) is present — adds status queue usage and supersede policy.
- **US3 (P2)**: After US1–US2 — cancellation + command types.
- **US4 (P2)**: After US1 — extends `tick` (can parallelise with US3 once `tick` stabilises).
- **US5 (P2)**: After US1 — primarily validation/tests; may touch `GradientEngine` / `MtcTickSource` documentation or thin guards.

### Within Each User Story

- Tests (T009, T015, …) before implementation when using TDD for that story.
- `ActiveFade` / `OscSender` before `FadeRegistry::tick`.
- `FadeRegistry` before `GradientEngine` integration.
- Engine integration before daemon application wiring.

### Parallel Opportunities

- **Phase 2**: T005 and T006 in parallel; T003 then T004 sequential.
- **US1**: T009 in parallel with early header-only work only if tests compile against stubs — otherwise run T009 first, then T010–T014.
- **US2–US5**: Test tasks marked **[P]** can run in parallel when targeting different sections of `test_fade_registry.cpp` if merge conflicts are avoided (prefer sequential edits to the same file in practice).
- **Polish**: T024, T025 in parallel.

---

## Parallel Example: User Story 1

```bash
# After Phase 2 completes, a split workflow might look like:
Task T009: "Add failing unit tests in tests/test_fade_registry.cpp …"
Task T005/T006: Already done in Foundational — ActiveFade + OscSender ready for T010–T011
```

---

## Parallel Example: User Story 4

```bash
# OSC failure tests (T020) can be authored while reviewing tick send semantics from US1:
Task T020: "Extend tests/test_fade_registry.cpp for OSC failure threshold …"
Task T021: "Extend FadeRegistry::tick in src/engine/FadeRegistry.cpp …"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup  
2. Complete Phase 2: Foundational (**critical**)  
3. Complete Phase 3: US1 (OSC path + engine wiring)  
4. **STOP and VALIDATE**: Run US1 tests; manual daemon smoke with loopback OSC  
5. Deploy/demo if ready  

### Incremental Delivery

1. Setup + Foundational → infrastructure ready  
2. Add **US1** → validate OSC curve output  
3. Add **US2** → validate `fade_complete` + supersede errors on NNG  
4. Add **US3** → cancel / project unload semantics  
5. Add **US4** → player failure visibility  
6. Add **US5** → transport edge cases  
7. Polish → SC-007 benchmark + docs  

### Parallel Team Strategy

1. Team completes Setup + Foundational together.  
2. After Foundational: Developer A focuses on **US1** (`FadeRegistry` + `GradientEngine`); Developer B prepares **US2** test doubles / NNG message assertions; Developer C drafts **US3–US5** tests.  
3. Converge on `test_fade_registry.cpp` with small, ordered merges.  

---

## Notes

- **[P]** = different files or non-conflicting work items.  
- **[USn]** maps tasks to spec.md user stories.  
- `StatusEmitRequest` strings on the hot tick path are acceptable only for **completion/error** events, not per-frame (data-model.md).  
- Commit after each task or logical group; stop at checkpoints to validate stories independently.  

---

## Format Validation

All tasks use the required checklist pattern: `- [x] Tnnn [optional P] [optional USn] Description with file path`.
