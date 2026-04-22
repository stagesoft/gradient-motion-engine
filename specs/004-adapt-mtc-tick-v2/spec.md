# Feature Specification: Adapt MtcTickSource to mtcreceiver v2.0.0

**Feature Branch**: `004-adapt-mtc-tick-v2`
**Created**: 2026-04-22
**Status**: Draft
**Input**: mtcreceiver has been updated (PRs #4 and #5 merged, tip 59fc76e, version 2.0.0) with a breaking API change. The existing MtcTickSource implementation in gradient-motion-engine is no longer valid and must be adapted.

## Clarifications

### Session 2026-04-22

- Q: Fate of Scenario B ("Both decode sites fire") → A: Repurpose into "Single-fire-per-QF contract" — verify `invokeTickForTesting` produces exactly one callback invocation per call, with `isCompleteFrame` correctly forwarded/ignored.
- Q: Callback dispatch latency target → A: p99 latency from `invokeTickForTesting` entry to consumer callback return MUST be <1 ms under nominal load (carried forward from spec 003).
- Q: Default `isCompleteFrame` value in rewritten test scenarios → A: Mixed realistic pattern — 7× `false` + 1× `true` per MTC full-frame cycle (every 8 QFs) across Scenarios A, C, and the synthetic stream to mirror real QF traffic.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Submodule bumped to v2.0.0 (Priority: P1)

A developer building gradient-motion-engine needs the project to compile and link against mtcreceiver v2.0.0. The pinned submodule commit must be bumped to `59fc76e` so that all downstream API changes are available.

**Why this priority**: Nothing else can proceed until the submodule points to the correct version — compilation fails against the old API.

**Independent Test**: Verify the submodule HEAD resolves to `59fc76e` and that a clean build completes without errors.

**Acceptance Scenarios**:

1. **Given** the repository is cloned with submodules, **When** the submodule is initialised and updated, **Then** `git -C <mtcreceiver-path> rev-parse HEAD` returns `59fc76e`.
2. **Given** the submodule is at `59fc76e`, **When** the project is built, **Then** no compilation errors arise from mtcreceiver header mismatches.

---

### User Story 2 - MtcTickSource registers callbacks via the new thread-safe API (Priority: P1)

A developer integrating gradient-motion-engine in a live performance system needs MtcTickSource to register and deregister callbacks without race conditions, using the new `MtcReceiver::setTickCallback()` method instead of the removed direct static assignment.

**Why this priority**: Without this change the code does not compile against v2.0.0, and the old pattern was undefined behaviour under concurrent access.

**Independent Test**: Construct a MtcTickSource, set a callback, trigger synthetic ticks via the testing helper, and confirm the callback fires exactly once per quarter-frame with the correct timestamp value.

**Acceptance Scenarios**:

1. **Given** a freshly constructed MtcTickSource, **When** `setTickCallback(cb)` is called, **Then** the callback is registered through `MtcReceiver::setTickCallback` and fires for subsequent ticks.
2. **Given** a callback is registered, **When** `setTickCallback({})` or `setTickCallback(nullptr)` is called, **Then** the callback is deregistered and no further invocations occur.
3. **Given** an existing callback, **When** a new callback is registered from a different thread, **Then** no crash, no undefined behaviour, and at most one callback is active at any moment.

---

### User Story 3 - MtcTickSource destructor guarantees no-call-after-destruction (Priority: P2)

A developer tearing down a gradient-motion-engine session needs the guarantee that destroying a MtcTickSource object immediately stops all pending or in-flight callbacks, preventing use-after-free crashes.

**Why this priority**: Critical for correct lifecycle management but can be addressed after basic compilation is restored.

**Independent Test**: Register a callback that accesses a local variable, destroy the MtcTickSource, then trigger a synthetic tick; no callback invocation must occur after destruction.

**Acceptance Scenarios**:

1. **Given** a MtcTickSource with an active callback, **When** the object is destroyed, **Then** the deregistration call is made during destruction, blocking until any in-flight invocation completes.
2. **Given** the MtcTickSource has been destroyed, **When** a synthetic tick is fired via the testing helper, **Then** the previously registered callback is not invoked.

---

### User Story 4 - Existing tests compile and pass against the new API (Priority: P2)

A developer running the test suite needs all MtcTickSource unit tests (Scenarios A, B, C, and the synthetic-stream test) to compile and pass against the new mtcreceiver testing helpers, without relying on the removed `MtcReceiver::onQuarterFrame` public variable.

**Why this priority**: Test coverage must be maintained; broken tests hide regressions.

**Independent Test**: Run `ctest` for the `test_mtc_tick_source` target and verify all cases pass.

**Acceptance Scenarios**:

1. **Given** the test binary is compiled with the testing flag enabled, **When** Scenario A (normal tick delivery) executes, **Then** all assertions pass using the `invokeTickForTesting` helper.
2. **Given** the test binary is compiled with the testing flag enabled, **When** Scenario B ("Single-fire-per-QF contract") executes, **Then** each `invokeTickForTesting` call produces exactly one callback invocation, and the `isCompleteFrame` flag is correctly forwarded/ignored by the adapter.
3. **Given** the synthetic-stream test, **When** 1200 ticks are fired at 10 ms intervals (12 s total at the real QF rate of 25 fps) using the 7×`false` + 1×`true` per-MTC-full-frame pattern, **Then** the consumer receives exactly 1200 callbacks and the final reported position matches the expected MTC head.
4. **Given** the spec text previously stated 60 s for SC-001/SC-002, **When** the tests are updated, **Then** the spec text is corrected to reflect 12 s / 1200 ticks.

---

### Edge Cases

- What happens when `setTickCallback` is called with an empty function object? The system must deregister any existing callback without crashing.
- What happens when MtcTickSource is destroyed while a tick is being processed? Destruction must block until the in-flight callback returns before releasing resources.
- What happens when ghost ticks arrive during an invalid QF sequence? With v2.0.0 these are suppressed upstream; the consumer must not receive or need to filter them.
- What happens when the first QFs arrive in reverse direction? v2.0.0 suppresses callbacks until direction stabilises; the consumer should expect 6 callbacks in a complete backwards stream rather than 8. **Scope note**: this behaviour is verified by mtcreceiver's own upstream unit tests — gradient-motion-engine does not re-verify it and exposes no code path that branches on direction, so no task in this feature covers it.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The mtcreceiver submodule MUST be pinned to commit `59fc76e` (mtcreceiver v2.0.0).
- **FR-002**: `MtcTickSource::setTickCallback(std::function<void(long)>)` MUST register the callback via `MtcReceiver::setTickCallback`, adapting the `void(long, bool)` v2.0.0 signature by ignoring the `isCompleteFrame` flag.
- **FR-003**: `MtcTickSource::setTickCallback` called with an empty/null function MUST deregister the callback via `MtcReceiver::setTickCallback({})`.
- **FR-004**: `MtcTickSource` MUST declare an explicit destructor (not `= default`) that calls `MtcReceiver::setTickCallback({})` to guarantee no-call-after-unregister.
- **FR-005**: All unit tests in `tests/test_mtc_tick_source.cpp` MUST be rewritten to use `MtcReceiver::invokeTickForTesting(long ms, bool isCompleteFrame)` and the `SkipPortOpenTag` constructor available under the testing compile flag, replacing all direct references to the removed `MtcReceiver::onQuarterFrame`. Scenario B MUST be repurposed to verify the v2.0.0 single-fire-per-QF contract: each `invokeTickForTesting` call produces exactly one callback invocation, and the `isCompleteFrame` flag is correctly forwarded/ignored by the adapter.
- **FR-006**: The synthetic-stream test MUST reflect 1200 ticks over 12 s at the real QF rate of 25 fps (100 Hz); any in-code comment or assertion inside `tests/test_mtc_tick_source.cpp` previously referencing 60 s must be corrected. **Scope**: "any comment or assertion" refers to in-code artifacts in this repository only — prior spec documents (e.g. spec 003) are historical and not subject to retroactive correction under this feature.
- **FR-007**: The build system MUST enable the mtcreceiver testing helpers when compiling the test target.
- **FR-008**: Rewritten Scenarios A, C, and the synthetic-stream test MUST drive `invokeTickForTesting` with a realistic MTC pattern — 7 calls with `isCompleteFrame=false` followed by 1 call with `isCompleteFrame=true` per MTC full-frame cycle (every 8 QFs) — to mirror real quarter-frame traffic.
- **FR-009**: The one-instance-per-process constraint carried forward from spec 003 (single `MtcTickSource` per process; mtcreceiver uses process-global static state) MUST be preserved. Constructing a second `MtcTickSource` while one already exists remains undefined behaviour and this feature does not introduce multi-instance support.
- **FR-010**: A regression test MUST verify that `MtcTickSource::setTickCallback()` is safe to call concurrently from multiple threads (US2 Acceptance Scenario 3): spawning a thread that repeatedly registers/deregisters callbacks while the main thread fires ticks must not crash, must not produce data races, and must leave at most one callback active at any moment.

### Key Entities

- **MtcTickSource**: Component that bridges mtcreceiver's quarter-frame tick events to the gradient-motion-engine consumer. Owns the callback registration lifecycle.
- **MtcReceiver v2.0.0**: External library (submodule). Exposes `setTickCallback(TickCallback)` with signature `void(long mtcHeadMs, bool isCompleteFrame)` and testing helpers available under a compile-time testing flag.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A clean build of gradient-motion-engine (library + daemon + tests) completes with zero errors and zero warnings related to mtcreceiver API usage after the submodule bump.
- **SC-002**: All `test_mtc_tick_source` scenarios (A, B, C, D, E, F, synthetic stream) pass, with the `test_mtc_tick_source` binary alone completing in under 30 seconds wall-clock time on reference hardware. (Scope is restricted to this test target, not the full `ctest` suite.)
- **SC-003**: The synthetic-stream test delivers exactly 1200 callbacks and reports a final MTC head that differs from the expected value by no more than 1 ms, covering 12 s of simulated playback at 25 fps.
- **SC-004**: Destroying a MtcTickSource during an active tick stream produces zero callback invocations after the destructor returns, with no crashes or memory-safety errors under a sanitiser build.
- **SC-005**: The project builds and all tests pass under a thread-safety checker with zero data-race reports in MtcTickSource and MtcReceiver code paths.
- **SC-006**: p99 callback dispatch latency — measured from `invokeTickForTesting` entry to consumer callback return, sampled across the full 1200-tick synthetic-stream test (SC-003) — MUST be under 1 ms on reference hardware. "Nominal playback load" is defined here as the synthetic stream cadence: 100 Hz sustained for 12 s with the 7×`false` + 1×`true` flag pattern.

## Assumptions

- Both mtcreceiver PRs (#4 and #5) are already merged to `stagesoft/mtcreceiver` main at commit `59fc76e`; no upstream changes are required.
- The `isCompleteFrame` flag introduced by the new API is intentionally ignored in gradient-motion-engine for this feature; exposing it to higher-level consumers is out of scope.
- The testing helper `invokeTickForTesting` and the `SkipPortOpenTag` constructor are available in mtcreceiver v2.0.0 when the appropriate compile-time testing flag is set, as documented in the handover notes.
- The real QF rate at 25 fps is 100 Hz (25 frames × 4 quarter-frames), not 200 Hz as stated in earlier spec drafts.
- Ghost ticks and double-fires in QF #8 are eliminated in v2.0.0 upstream; no consumer-side deduplication logic is needed.
- Target platform is Linux (GCC, `-Wall -O3 -pthread`); mobile and web are out of scope.
