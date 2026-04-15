# Feature Specification: Gradient Curves Module (gme::gradient)

**Feature Branch**: `002-gradient-curves`  
**Created**: 2026-04-15  
**Status**: Draft  
**Input**: User description: "Develop the requirements for Phase 1, based on the description layed out at dev/C++ Node-Level FadeEngine — Implementation Plan.md"

## Clarifications

### Session 2026-04-15

- Q: Should out-of-range input be clamped before or after curve evaluation? → A: Clamp input to [0, 1] before evaluation (Option A). Curve always receives valid domain; output is guaranteed within expected range.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Evaluate a Fade Curve at Any Point in Time (Priority: P1)

A fade engine evaluating a running fade needs to look up the interpolated value for a given progress point. The gradient module must accept a normalized progress (0.0 to 1.0) and return the corresponding output value (0.0 to 1.0) for any supported curve shape. This is the fundamental operation that all downstream fade processing depends on.

**Why this priority**: Without curve evaluation, no fade of any kind can produce intermediate values. Every other feature depends on this.

**Independent Test**: Can be fully tested by instantiating any curve type, calling evaluate at known progress points, and verifying the returned values against mathematically expected results. Delivers the core interpolation capability.

**Acceptance Scenarios**:

1. **Given** a linear curve, **When** evaluate is called with progress 0.0, **Then** the result is 0.0
2. **Given** a linear curve, **When** evaluate is called with progress 1.0, **Then** the result is 1.0
3. **Given** a linear curve, **When** evaluate is called with progress 0.5, **Then** the result is 0.5
4. **Given** a sigmoid curve with default parameters, **When** evaluate is called with progress 0.0 and 1.0, **Then** the results are 0.0 and 1.0 respectively (normalized endpoints)
5. **Given** any monotonically increasing curve, **When** evaluate is called with a sequence of increasing progress values, **Then** the output values are non-decreasing

---

### User Story 2 - Select a Curve Shape by Name (Priority: P1)

An operator or cue designer configures a fade and selects a curve shape (e.g., "sigmoid", "bezier", "linear") along with optional shape parameters. The system must be able to create the correct curve from a name string and parameter set without requiring the caller to know the specific class.

**Why this priority**: The upstream command protocol delivers curve type as a string. Without name-based construction, no fade command from the controller can be translated into a working curve instance.

**Independent Test**: Can be tested by requesting curves by name with varying parameter sets and verifying the returned curve evaluates correctly for that shape. Delivers the ability to map user/protocol input to working curve objects.

**Acceptance Scenarios**:

1. **Given** a valid curve type name "sigmoid" and valid parameters, **When** a curve is requested, **Then** a sigmoid curve instance is returned that evaluates correctly
2. **Given** a valid curve type name "linear" with no parameters, **When** a curve is requested, **Then** a linear curve instance is returned
3. **Given** an unknown curve type name, **When** a curve is requested, **Then** the system falls back to a linear curve and reports a warning
4. **Given** a valid curve type name with missing optional parameters, **When** a curve is requested, **Then** reasonable defaults are applied

---

### User Story 3 - Performant Curve Evaluation During Playback (Priority: P1)

During live playback, the engine evaluates curves at high frequency (200-240 Hz per fade). Complex mathematical curves (sigmoid with exponential, bezier with iterative solving) must not introduce computational latency. A pre-computed lookup table with interpolation must be available to guarantee constant-time evaluation during ticking.

**Why this priority**: Real-time playback cannot tolerate per-tick transcendental math. Without efficient evaluation, fades will cause jitter or missed ticks in production.

**Independent Test**: Can be tested by wrapping any curve in a resampled (LUT-backed) version, evaluating at many points, and verifying both accuracy (close to original curve) and that the lookup operation is bounded in time. Delivers production-grade evaluation performance.

**Acceptance Scenarios**:

1. **Given** a complex curve wrapped in a resampled version with 256 samples, **When** evaluate is called at an arbitrary progress point, **Then** the result is within an acceptable tolerance of the unwrapped curve's result
2. **Given** a resampled curve, **When** evaluate is called at any progress value, **Then** the operation performs a table lookup and linear interpolation (no transcendental math)
3. **Given** a curve requested through the factory, **When** no explicit opt-out is provided, **Then** the returned curve is wrapped in a resampled version by default

---

### User Story 4 - Remap Curve Output to Arbitrary Value Ranges (Priority: P2)

Fades operate on different parameter ranges: audio volume is 0.0-1.0, but future signals might need different ranges (e.g., DMX 0-255, or partial fades from 0.3 to 0.8). A decorator must allow any curve to be remapped to arbitrary input and output ranges without modifying the curve itself.

**Why this priority**: Audio and video fades in the current system both use 0.0-1.0, but the module is designed as a general-purpose signal library. Range remapping is needed for partial fades (e.g., fade from 30% to 80% volume) and future signal types.

**Independent Test**: Can be tested by wrapping a known curve with a range decorator, evaluating, and verifying the output is correctly scaled. Delivers flexible value mapping without curve modification.

**Acceptance Scenarios**:

1. **Given** a linear curve wrapped with output range [0.0, 0.5], **When** evaluate is called with progress 1.0, **Then** the result is 0.5
2. **Given** a curve wrapped with input range [100, 200], **When** evaluate is called with input 150, **Then** the internal curve receives progress 0.5
3. **Given** a curve wrapped with both input and output remapping, **When** evaluated at boundaries, **Then** results match the mapped range endpoints exactly

---

### User Story 5 - Generate Complementary Crossfade Values (Priority: P2)

When crossfading between two sources (e.g., fading out one audio cue while fading in another), the system must guarantee that the two fade values always sum to 1.0 at every point. A single curve evaluation must produce both the primary and complementary values to prevent drift or floating-point divergence.

**Why this priority**: Crossfading is a planned feature (Phase 7) and the data structure must be in place. However, it is not needed for basic single-direction fades which are the first use case.

**Independent Test**: Can be tested by creating a crossfade pair, evaluating at many points, and verifying the primary + complementary values sum to exactly 1.0 at each point. Delivers guaranteed energy-preserving crossfades.

**Acceptance Scenarios**:

1. **Given** a crossfade pair using a sigmoid curve, **When** evaluate is called at any progress point, **Then** the primary and complementary values sum to exactly 1.0
2. **Given** a crossfade pair, **When** evaluate is called at progress 0.0, **Then** the primary value is 0.0 and the complementary value is 1.0
3. **Given** a crossfade pair, **When** evaluate is called at progress 1.0, **Then** the primary value is 1.0 and the complementary value is 0.0

---

### Edge Cases

- What happens when evaluate is called with progress values outside [0, 1] (e.g., -0.1, 1.5)? Input must be clamped to [0, 1] before evaluation, so the curve always receives a valid domain value and output is guaranteed within the expected range.
- What happens when a sigmoid curve is given extreme steepness values? The curve must still produce 0.0 at t=0 and 1.0 at t=1 due to normalization.
- What happens when a bezier curve's control points create a non-monotonic shape? The module evaluates the mathematical curve as-is; monotonicity is the responsibility of the curve designer, not enforced at evaluation time.
- What happens when a resampled curve has very few samples (e.g., N=2)? The system must accept any positive sample count, though accuracy degrades with fewer samples.
- What happens when the factory receives malformed or empty parameter data? Defaults must be applied for missing fields; the system must not crash on bad input.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST provide an abstract curve interface that accepts a normalized input (0.0 to 1.0) and returns a normalized output (0.0 to 1.0). Input values outside [0, 1] MUST be clamped to [0, 1] before evaluation
- **FR-002**: System MUST provide a linear curve implementation where output equals input
- **FR-003**: System MUST provide a sigmoid curve implementation with configurable steepness and midpoint parameters, normalized so that evaluate(0)=0 and evaluate(1)=1
- **FR-004**: System MUST provide a cubic bezier curve implementation with two configurable control points, using De Casteljau evaluation
- **FR-005**: System MUST provide ease-in (power curve: t^exp) and ease-out (1-(1-t)^exp) curve implementations with a configurable exponent
- **FR-006**: System MUST provide an S-curve implementation using the smoothstep polynomial (3t^2 - 2t^3)
- **FR-007**: System MUST provide a scaling decorator that remaps any curve's input range from [inMin, inMax] to [0, 1] and output range from [0, 1] to [outMin, outMax]
- **FR-008**: System MUST provide a resampled curve decorator that pre-computes an N-sample lookup table at construction and performs linear interpolation at evaluation time, with a default sample count of 256
- **FR-009**: System MUST provide a crossfade pair that wraps a single curve and returns both the evaluated value and its complement (1.0 minus evaluated value) to guarantee the pair always sums to 1.0
- **FR-010**: System MUST provide a factory that creates curve instances from a type name string and a parameter set, returning a resampled-wrapped curve by default
- **FR-011**: The factory MUST fall back to a linear curve when given an unrecognized curve type name, and report a warning
- **FR-012**: All curve implementations MUST be self-contained with no dependencies on daemon logic, communication protocols, or system-specific services

### Key Entities

- **Curve**: The abstract evaluation interface. Accepts normalized progress, returns normalized value. All concrete curve types and decorators implement this interface.
- **CurveFactory**: Translates a string type name and parameter set into a concrete Curve instance. The single point of entry for creating curves from external command data.
- **ResampledCurve**: A decorator that wraps any Curve with a pre-computed lookup table for constant-time evaluation. The default production wrapper.
- **ScaledCurve**: A decorator that wraps any Curve to remap its input and output value ranges.
- **CrossfadePair**: A wrapper that holds one Curve and produces complementary (primary, 1-primary) output pairs for energy-preserving crossfades.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All curve types produce 0.0 at progress 0.0 and 1.0 at progress 1.0 (boundary correctness verified for every implementation)
- **SC-002**: Resampled curve output deviates from the original curve by no more than 0.5% at any evaluated point (with default 256 samples)
- **SC-003**: Crossfade pair values sum to exactly 1.0 at every evaluated point
- **SC-004**: All supported curve types (linear, sigmoid, bezier, ease-in, ease-out, s-curve) can be instantiated by name through the factory
- **SC-005**: Factory handles unknown curve type names gracefully without failure, falling back to a default shape
- **SC-006**: The module has no dependencies on daemon code, communication protocols, or OS-specific services — it can be built and tested in isolation

## Assumptions

- The curve module operates purely on normalized [0, 1] domains; the caller (engine layer) is responsible for converting timecode progress and parameter ranges before and after evaluation
- Curves are constructed once per fade and evaluated many times; construction-time cost (including LUT pre-computation) is acceptable, evaluation-time cost must be minimal
- Single-threaded evaluation per curve instance: the engine tick loop evaluates fades sequentially, so curves do not need internal thread safety
- The sigmoid normalization approach (subtracting f(0) and scaling so f(1)=1) is acceptable even though it slightly alters the mathematical shape near the endpoints
- The bezier implementation uses a parametric cubic bezier (not an x-y mapping bezier), meaning the parameter t directly maps to curve progress
- The number of distinct curve types is small and known at compile time; the factory does not need a plugin or runtime registration mechanism
