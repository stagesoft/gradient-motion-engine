# Implementation Plan: Adapt MtcTickSource to mtcreceiver v2.0.0

**Branch**: `004-adapt-mtc-tick-v2` | **Date**: 2026-04-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/004-adapt-mtc-tick-v2/spec.md`

## Summary

`mtcreceiver` has released v2.0.0 (commit `59fc76e`, PRs #4 and #5 merged). The
breaking change removes the public static `MtcReceiver::onQuarterFrame`
`std::function` in favour of a thread-safe registration API
`MtcReceiver::setTickCallback(TickCallback)` whose signature is now
`void(long mtcHeadMs, bool isCompleteFrame)`. QF #8 has been collapsed from a
double-fire (Site 1 + Site 2) to a single-fire with `isCompleteFrame=true`. New
testing helpers (`invokeTickForTesting`, `SkipPortOpenTag` constructor,
`resetDecoderStateForTesting`, `resetStaticStateForTesting`) are exposed when
`-DMTCRECV_TESTING` is defined.

`gradient-motion-engine` must adapt: bump the submodule pin, rewrite
`MtcTickSource::setTickCallback` as a thin adapter that ignores
`isCompleteFrame`, add an explicit destructor that calls
`MtcReceiver::setTickCallback({})` to guarantee no-call-after-destruction, and
rewrite all unit tests to drive the new testing helpers with a realistic MTC
pattern (7×`false` + 1×`true` per MTC full-frame cycle). Synthetic-stream test
is corrected to 1200 ticks × 10 ms = 12 s (not the earlier 60 s typo in
spec 003).

## Technical Context

**Language/Version**: C++17 (GCC, `-Wall -O3 -pthread`)
**Primary Dependencies**: mtcreceiver v2.0.0 (submodule, pinned at `59fc76e`),
RtMidi (via pkg-config), cuemslogger (submodule, optional), standard library
only for the adapter (`<functional>`, `<memory>`, `<string>`, `<atomic>`)
**Storage**: N/A (in-memory adapter; no persistence)
**Testing**: CTest + hand-rolled `CHECK` macros in `tests/test_mtc_tick_source.cpp`;
mtcreceiver testing helpers enabled via `-DMTCRECV_TESTING` on the test target only
**Target Platform**: Linux (Debian/Ubuntu, GCC); no mobile/web
**Project Type**: C++ library (`libgradient_motion`) + thin daemon
(`gradient-motiond`) consumer
**Performance Goals**: p99 callback dispatch latency <1 ms from
mtcreceiver tick entry point to consumer callback return (SC-006); zero heap
allocation per tick in steady state (adapter captures `cb` by value once at
registration)
**Constraints**: Callback fires from the MIDI thread — must remain lock-free
and non-blocking. No exceptions across library boundaries. Submodule HEAD must
match `59fc76e` byte-for-byte (FR-001).
**Scale/Scope**: One `MtcTickSource` instance per process (unchanged from
spec 003). Single source file (`src/time/MtcTickSource.{h,cpp}`), single
test file (`tests/test_mtc_tick_source.cpp`), one CMakeLists update.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Deterministic Evaluation | ✅ PASS | Adapter is a pure pass-through of `(long ms)` to the consumer. `isCompleteFrame` is ignored, introducing no hidden state. No wall-clock, randomness, or environment dependence. |
| II. Modular Architecture | ✅ PASS | Change is confined to `gme::time`. No new cross-module dependencies introduced. |
| III. Library-First | ✅ PASS | All behaviour lives in `libgradient_motion`; daemon is unaffected. |
| IV. Real-Time Safety | ✅ PASS | Adapter lambda captures the consumer `cb` by move once at registration — zero allocation on the hot path. Destructor calls `setTickCallback({})` which is the v2.0.0 guaranteed-unregister path; blocking is bounded to the duration of one in-flight callback (which is itself bounded by the consumer's lock-free contract). |
| V. Protocol-Agnostic Core | ✅ PASS | No OSC or transport-layer changes. |
| VI. Documentation Standards | ✅ PASS (action required) | Updated docstrings must reflect the new adapter behaviour, the destructor contract, and the corrected QF cadence (100 Hz at 25 fps, not 200 Hz). Captured concretely in tasks T006 (setTickCallback docstring), T009 (destructor docstring), and T026 (final v1-reference audit). |

**Gate verdict**: PASS. No complexity deviations to justify.

## Project Structure

### Documentation (this feature)

```text
specs/004-adapt-mtc-tick-v2/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (C++ header contract)
│   └── MtcTickSource.h
├── checklists/
│   └── requirements.md  # Spec quality checklist (already present)
├── spec.md              # Feature specification
└── tasks.md             # Phase 2 output (/speckit.tasks — NOT created here)
```

### Source Code (repository root)

```text
src/
├── time/
│   ├── MtcTickSource.h     # Docstrings updated; explicit ~MtcTickSource() declared
│   └── MtcTickSource.cpp   # setTickCallback → adapter; destructor definition added
├── gradient/               # (untouched)
├── motion/                 # (untouched)
├── signal/                 # (untouched)
├── engine/                 # (untouched)
└── osc/                    # (untouched)

tests/
├── CMakeLists.txt          # Add -DMTCRECV_TESTING to test_mtc_tick_source target
├── test_mtc_tick_source.cpp# Rewritten: invokeTickForTesting + SkipPortOpenTag
└── test_curves.cpp         # (untouched)

mtcreceiver/                # Submodule — bumped from 615805a to 59fc76e

daemon/                     # (untouched — only recompiled against the new header)
CMakeLists.txt              # (untouched unless research.md finds build wiring gaps)
```

**Structure Decision**: Single-project C++ layout, already established by
spec 001 and spec 003. This feature touches exactly three paths:
`src/time/MtcTickSource.{h,cpp}`, `tests/test_mtc_tick_source.cpp`,
`tests/CMakeLists.txt`, plus the `mtcreceiver/` submodule commit pointer.

## Complexity Tracking

No constitution gates fail. No deviations to justify.
