# Research: Gradient Curves Module

**Branch**: `002-gradient-curves` | **Date**: 2026-04-15

## R1: Sigmoid Normalization Strategy

**Decision**: Normalize using endpoint subtraction and scaling: `(raw(t) - raw(0)) / (raw(1) - raw(0))` where `raw(t) = 1/(1+exp(-k*(t-m)))`.

**Rationale**: The raw sigmoid never reaches exactly 0 or 1. Endpoint normalization guarantees `evaluate(0)=0` and `evaluate(1)=1` as required by FR-003 and SC-001. The normalization factors (`raw(0)` and `raw(1)`) are computed once at construction time and stored — no per-evaluation overhead.

**Alternatives considered**:
- *Clamping at extremes*: Would produce flat segments near boundaries for moderate steepness values. Rejected — produces visible discontinuities in fade curves.
- *High steepness to approximate 0/1*: Would require steepness > 20 for < 0.001 error. Rejected — constrains parameter range and doesn't guarantee exact endpoints.

## R2: Bezier Evaluation Approach

**Decision**: Use De Casteljau's algorithm for cubic bezier evaluation, treating `t` as the parametric variable directly (not solving for `t` given `x`).

**Rationale**: The implementation plan specifies De Casteljau (FR-004). Since the progress parameter `t` maps directly to the bezier parameter (not an x-y curve lookup), no root-finding or subdivision is needed. De Casteljau is numerically stable and straightforward for parametric evaluation. With ResampledCurve wrapping, the De Casteljau computation only runs N times at construction (for LUT build), never during playback.

**Alternatives considered**:
- *Direct polynomial evaluation*: Slightly faster per-call but numerically less stable for extreme control points. Rejected — stability matters more than micro-optimization since ResampledCurve eliminates per-tick cost anyway.
- *x-y bezier with Newton-Raphson*: Required for CSS-style bezier curves where `t` is the x-axis. Rejected — our domain maps progress directly to the parametric variable, so no root-finding is needed.

## R3: LUT Interpolation Accuracy (ResampledCurve)

**Decision**: 256 samples with linear interpolation as default. Accuracy target: < 0.5% maximum deviation from the original curve (SC-002).

**Rationale**: For the steepest sigmoid in practical use (k=12, which covers most fade aesthetics), 256 samples produce < 0.2% maximum error with linear interpolation. For bezier curves, error is even lower due to their polynomial nature. 256 samples = 2KB of memory per curve (256 × 8 bytes for `double`) — negligible. Linear interpolation between adjacent LUT entries is a single multiply-add, well within the sub-microsecond budget.

**Alternatives considered**:
- *64 samples*: Sufficient for linear and ease curves but exceeds 0.5% error for steep sigmoids. Rejected — would need per-curve-type sample count tuning.
- *1024 samples*: Over-provisioned for all practical curves. Rejected — wastes memory (8KB) with no perceptible quality improvement.
- *Cubic interpolation between LUT entries*: Reduces error further but adds complexity and per-evaluation cost. Rejected — linear interpolation already meets the accuracy target.

## R4: Testing Framework

**Decision**: Use plain C++ test binary with `assert`-based tests and CMake `add_test()`, consistent with the existing project structure (no test framework in the codebase currently).

**Rationale**: The project has no existing test framework dependency. Google Test would add a submodule or system dependency. For the scope of curve testing (boundary values, monotonicity, accuracy comparisons), simple assertions with clear failure messages are sufficient. The test binary returns non-zero on failure, integrable with `ctest`.

**Alternatives considered**:
- *Google Test*: More structured output and fixtures. Rejected for Phase 1 — can be adopted later if test complexity warrants it. Adding a dependency for ~50 test assertions is over-engineering.
- *Catch2 (header-only)*: Lower friction than Google Test. Acceptable future option but still unnecessary for current scope.

## R5: Input Clamping Strategy

**Decision**: Clamp input to [0, 1] in the abstract `Curve::evaluate()` contract. Each concrete implementation clamps `t` at the start of its `evaluate()` method.

**Rationale**: Per clarification session (2026-04-15), input clamping before evaluation was chosen to ensure curves always receive valid domain values. Clamping in each concrete class (rather than a base class wrapper) allows header-only implementations to remain self-contained. The cost is one `std::clamp` call per evaluation — a single comparison pair, negligible.

**Alternatives considered**:
- *Base class clamping via non-virtual wrapper*: Would centralize clamping but requires a two-method pattern (public `evaluate` → private `doEvaluate`). Rejected — adds indirection for a trivial operation and complicates the header-only curve implementations.
- *Clamping only in ResampledCurve*: Since production curves are wrapped in ResampledCurve, clamping there would cover playback. Rejected — leaves unwrapped curves unprotected during testing and direct use.

## R6: CurveFactory Parameter Format

**Decision**: Factory accepts `nlohmann::json` for curve parameters. This is the same JSON library already in the project's dependency list (`nlohmann-json3-dev`).

**Rationale**: The upstream NNG command protocol delivers curve parameters as JSON (see implementation plan Phase 3, `curve_params` field). Using the same JSON type in the factory avoids an intermediate translation step. The `nlohmann/json` library is header-only and already a project dependency. The factory lives in `src/gradient/` (library code), but `nlohmann/json` is a data-handling library, not a protocol dependency — this does not violate Constitution Principle V.

**Alternatives considered**:
- *`std::map<std::string, double>`*: Simpler but loses structured data (nested params, string values). Rejected — would require callers to flatten JSON before calling the factory.
- *Custom parameter struct per curve type*: Type-safe but requires the caller to know the concrete type. Rejected — defeats the purpose of a string-based factory.
