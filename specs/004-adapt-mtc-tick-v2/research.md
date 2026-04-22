# Phase 0 Research: Adapt MtcTickSource to mtcreceiver v2.0.0

**Feature**: 004-adapt-mtc-tick-v2
**Date**: 2026-04-22

All Technical Context entries in `plan.md` are fully resolved — no
`NEEDS CLARIFICATION` markers remain. This document captures the decisions
that anchor the implementation and the alternatives that were weighed.

## Decision 1 — Submodule pin

- **Decision**: Bump `mtcreceiver/` from `615805a` (PR#3 tip) to `59fc76e`
  (v2.0.0 tip, PRs #4 and #5 merged).
- **Rationale**: v2.0.0 removes the public `onQuarterFrame` static variable
  (used today in `src/time/MtcTickSource.cpp:14` and throughout
  `tests/test_mtc_tick_source.cpp`), so the project does not compile against
  any commit older than `59fc76e`. The pin is mandated by FR-001.
- **Alternatives considered**:
  - Vendor the v2 headers into the project — rejected, violates the
    project-wide "git submodules for 3rd-party/shared libs" convention.
  - Stay on `615805a` and patch locally — rejected, upstream already fixed
    the thread-safety UB and the patch would drift.

## Decision 2 — Adapter strategy (ignore `isCompleteFrame`)

- **Decision**: `MtcTickSource::setTickCallback(std::function<void(long)>)`
  wraps the consumer callback in a lambda that matches the v2.0.0 signature
  `void(long ms, bool isCompleteFrame)` and discards the second argument.
  When called with an empty/null `std::function`, the adapter calls
  `MtcReceiver::setTickCallback({})` to deregister.
- **Rationale**: The existing consumer (`GradientEngine` via the daemon) has
  no notion of full-frame authoritative positions. The flag is preserved
  inside mtcreceiver for future consumers but is currently out-of-scope for
  the engine (see `spec.md` Assumptions §2). A pass-through adapter keeps the
  public `MtcTickSource` API source-compatible with every call site from
  spec 003. Capturing the consumer callback by move once at registration
  means zero allocation on the hot path (Principle IV).
- **Alternatives considered**:
  - Expose `isCompleteFrame` up through `MtcTickSource` — rejected, widens
    the public API without a validated consumer need and would require
    updating the daemon plus all engine-integration tests.
  - Register two callbacks (one for each flag value) — rejected, not
    supported by the v2 API (one slot), and would reintroduce the
    double-fire semantics PR#4 was written to eliminate.

## Decision 3 — Destructor contract

- **Decision**: `MtcTickSource` declares an explicit destructor
  (`~MtcTickSource();`) that calls `MtcReceiver::setTickCallback({})` before
  any other teardown. The current `= default` destructor is replaced.
- **Rationale**: v2.0.0 guarantees that `setTickCallback({})` blocks until
  any in-flight invocation returns (per mtcreceiver handover notes). This
  is the only way to make "no call after destruction" a hard contract when
  the consumer callback captures references into the `MtcTickSource` owner
  scope. Required by FR-004 and SC-004.
- **Alternatives considered**:
  - Rely on `std::unique_ptr<MtcReceiver>` destruction to tear everything
    down — rejected, the callback slot is a static member of
    `MtcReceiver`; it outlives the unique_ptr and would fire a dangling
    closure if a late tick landed mid-destruction.
  - Set a flag and let the consumer check it — rejected, pushes complexity
    onto every consumer and races the MIDI thread.

## Decision 4 — Test helpers: `invokeTickForTesting` + `SkipPortOpenTag`

- **Decision**: Replace all direct uses of `MtcReceiver::onQuarterFrame(...)`
  with `MtcReceiver::invokeTickForTesting(long ms, bool isCompleteFrame)`.
  Construct the `MtcReceiver` via the new `SkipPortOpenTag` constructor
  overload so no real MIDI port is opened in unit tests. Enable both helpers
  by adding `-DMTCRECV_TESTING` as a compile definition on the
  `test_mtc_tick_source` target only (not on the library or daemon).
- **Rationale**: `onQuarterFrame` is gone upstream; `invokeTickForTesting`
  is the single documented entry point for driving the tick path in unit
  tests. `SkipPortOpenTag` avoids the existing workaround in Scenario E
  that currently catches "either `kNoPortsAvailable` or `kPortNotFound`" —
  with the skip-port constructor, tests become deterministic across hosts
  with and without MIDI hardware. Confining `-DMTCRECV_TESTING` to the test
  target keeps the production library and daemon build identical to
  spec 003's contract.
- **Alternatives considered**:
  - Add a small in-test shim that poked private decoder functions —
    rejected, defeats the purpose of the upstream testing API and
    reintroduces coupling to internals.
  - Enable `-DMTCRECV_TESTING` globally — rejected, the flag compiles in
    helper symbols that are strictly test-only; leaking them into
    production is a footgun.

## Decision 5 — Scenario B repurposed to "single-fire-per-QF"

- **Decision**: Scenario B is repurposed from "both decode sites fire" to
  "single-fire-per-QF contract": each `invokeTickForTesting` call produces
  exactly one consumer callback invocation, and the adapter correctly
  forwards/ignores the `isCompleteFrame` flag. The old assertion (`received.size() == 2`)
  is replaced with `received.size() == N` where `N` equals the number of
  `invokeTickForTesting` calls in the scenario, regardless of flag value.
- **Rationale**: v2.0.0 collapses QF#8 from double-fire to single-fire with
  a flag. Keeping the old assertion would make the test a regression
  detector for behaviour that is no longer desired. Clarification
  Q1/2026-04-22 locks this decision.
- **Alternatives considered**:
  - Delete Scenario B entirely — rejected, loses coverage of the single-fire
    invariant that is the whole point of PR#4.

## Decision 6 — Synthetic-stream cadence correction

- **Decision**: The synthetic-stream test drives exactly 1200
  `invokeTickForTesting` calls with 10 ms between calls. Total modelled
  duration is 12 s at the real QF rate of 100 Hz (25 fps × 4 QF). The
  spec text (SC-001 / SC-002 from spec 003) is corrected from "60 seconds"
  to "12 seconds" (FR-006, SC-003).
- **Rationale**: Handover notes confirm 1200 × 10 ms = 12 s is the real
  cadence; the earlier "60 s" figure was a stale draft. SC-002 imposes a
  <30 s total test runtime, which 12 s of synthetic ticks easily satisfies.
- **Alternatives considered**:
  - Expand the test to 6000 ticks to match the original "60 s" text —
    rejected, 5× runtime for no extra coverage (the decoder state machine
    is exercised well before 12 s elapse).

## Decision 7 — Mixed `isCompleteFrame` pattern in Scenarios A, C, synthetic

- **Decision**: Scenarios A, C, and the synthetic-stream test drive the
  helper with a realistic MTC pattern — 7 calls with `isCompleteFrame=false`
  followed by 1 call with `isCompleteFrame=true`, repeating every 8 QFs.
  Scenario B additionally asserts that the `true` variant produces exactly
  one callback (not two) and carries the same `ms` value the adapter was
  given.
- **Rationale**: Mirrors real quarter-frame traffic, so the adapter's
  ignore-flag behaviour is tested under the same ratios the MIDI thread
  would actually produce. Clarification Q3/2026-04-22 locks this choice.
- **Alternatives considered**:
  - Always `false` — rejected, never exercises the "QF#8 now single-fires
    with `true`" path and would pass even if the adapter accidentally
    skipped `true` ticks.
  - Always `true` — rejected, unrealistic and would not surface a bug
    where the adapter only forwarded `true` ticks.

## Decision 8 — Latency SLO carry-forward

- **Decision**: The p99 callback dispatch latency target (<1 ms from
  `invokeTickForTesting` entry to consumer callback return, under nominal
  load) is carried forward verbatim from spec 003. No new benchmark
  harness is required for this spec — the existing adapter is a single
  move + function call in front of a lambda, structurally faster than the
  spec 003 baseline.
- **Rationale**: Clarification Q2/2026-04-22 locks the target. No
  architectural changes that would plausibly regress the measured latency;
  deferring an explicit benchmark keeps scope tight.
- **Alternatives considered**:
  - Add a dedicated microbenchmark target — rejected for this feature;
    would be a standalone follow-up if SC-006 ever fails in practice.

## Open questions / deferred items

- None. All three clarification questions asked during `/speckit.clarify`
  resolved high-impact ambiguities; no deferred items block Phase 1.
