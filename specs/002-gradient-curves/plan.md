# Implementation Plan: Gradient Curves Module (gme::gradient)

**Branch**: `002-gradient-curves` | **Date**: 2026-04-15 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/002-gradient-curves/spec.md`

## Summary

Implement the `gme::gradient` module — a self-contained curve evaluation library providing the mathematical interpolation primitives for the fade engine. Includes 6 concrete curve types (linear, sigmoid, bezier, ease-in, ease-out, s-curve), 3 decorators (scaled, resampled, crossfade pair), and a string-based factory. All code lives in `src/gradient/` as part of `libgradient_motion`. No external dependencies beyond the C++ standard library.

## Technical Context

**Language/Version**: C++17 (GCC, `-Wall -O3 -pthread`)
**Primary Dependencies**: None (C++ standard library only — `<cmath>`, `<vector>`, `<memory>`, `<string>`, `<functional>`)
**Storage**: N/A
**Testing**: Google Test or plain `assert`-based test binary (no test framework currently in the project — see research.md)
**Target Platform**: Linux (Debian, same as CUEMS node infrastructure)
**Project Type**: Static library (`libgradient_motion`)
**Performance Goals**: Evaluation must complete within the 1 ms per-tick budget (constitution). At 240 Hz tick rate with multiple concurrent fades, each curve evaluation must be sub-microsecond. ResampledCurve (LUT + linear interpolation) achieves this.
**Constraints**: Zero heap allocations per evaluation in steady state (constitution). LUT is pre-allocated at construction time; evaluate() performs only array indexing and arithmetic.
**Scale/Scope**: Typically 1-4 concurrent fades per node. ~12 source files (headers + implementations) in `src/gradient/`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
| --------- | ------ | ----- |
| I. Deterministic Evaluation | PASS | All curves are pure functions: `evaluate(t)` depends only on `t` and construction-time parameters. No mutable state, no randomness, no wall-clock time. |
| II. Modular Architecture | PASS | `gme::gradient` has no dependencies on other `gme::*` modules. Independently compilable and testable. |
| III. Library-First | PASS | All code in `src/gradient/` as part of `libgradient_motion`. No daemon, systemd, or deployment dependencies. |
| IV. Real-Time Safety | PASS | `ResampledCurve::evaluate()` performs only array lookup + linear interpolation (no allocations, no exceptions, no I/O). Construction-time LUT computation is exempt per constitution. CurveFactory creates `unique_ptr` at construction time (exempt). |
| V. Protocol-Agnostic Core | PASS | No transport, serialization, or protocol awareness. Factory accepts `std::string` + JSON params — generic data, not protocol-specific. |
| VI. Documentation Standards | MUST COMPLY | All public classes and methods require Doxygen-compatible docstrings with brief, params, long description, errors, and example code. |

**Result**: All gates pass. No violations to justify.

## Project Structure

### Documentation (this feature)

```text
specs/002-gradient-curves/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── curve-interface.md
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/gradient/
├── Curve.h              # Abstract base interface
├── LinearCurve.h        # Header-only: output = input
├── SigmoidCurve.h       # Declaration
├── SigmoidCurve.cpp     # Normalized sigmoid: 1/(1+exp(-k*(t-m)))
├── EaseInCurve.h        # Header-only: t^exp
├── EaseOutCurve.h       # Header-only: 1-(1-t)^exp
├── SCurve.h             # Header-only: 3t^2-2t^3
├── BezierCurve.h        # Declaration
├── BezierCurve.cpp      # Cubic bezier, De Casteljau
├── ScaledCurve.h        # Header-only decorator: range remapping
├── ResampledCurve.h     # Declaration
├── ResampledCurve.cpp   # LUT pre-compute + linear interpolation
├── CrossfadePair.h      # Header-only: complementary pair
└── CurveFactory.h       # Declaration
    CurveFactory.cpp     # String → Curve factory

tests/
└── test_curves.cpp      # Unit tests for all curve types
```

**Structure Decision**: Follows the existing `src/gradient/` module directory established in Phase 0. The placeholder `gradient.cpp` will be removed and replaced with the concrete implementations above. Header-only for simple curves (linear, ease-in/out, s-curve, scaled, crossfade pair); separate `.cpp` for curves requiring non-trivial computation (sigmoid, bezier, resampled, factory).

## Complexity Tracking

No constitution violations. Table not required.
