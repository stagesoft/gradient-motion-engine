# Quickstart: Gradient Curves Module

**Branch**: `002-gradient-curves` | **Date**: 2026-04-15

## What This Module Does

The `gme::gradient` module provides mathematical curve evaluation for the fade engine. Given a progress value (0.0 = start, 1.0 = end), it returns the interpolated output value (0.0 to 1.0) shaped by the selected curve type (linear, sigmoid, bezier, etc.).

## Where It Lives

All source files go in `src/gradient/` as part of the `libgradient_motion` static library. Tests go in `tests/test_curves.cpp`.

## Key Files to Implement

| File | Purpose | Complexity |
| ---- | ------- | ---------- |
| `Curve.h` | Abstract interface with virtual `evaluate(t)` | Trivial |
| `LinearCurve.h` | Identity curve, header-only | Trivial |
| `SigmoidCurve.h/.cpp` | Normalized sigmoid with steepness/midpoint | Medium |
| `BezierCurve.h/.cpp` | Cubic bezier via De Casteljau | Medium |
| `EaseInCurve.h` | Power curve `t^exp`, header-only | Trivial |
| `EaseOutCurve.h` | Inverse power `1-(1-t)^exp`, header-only | Trivial |
| `SCurve.h` | Smoothstep polynomial, header-only | Trivial |
| `ScaledCurve.h` | Range remapping decorator, header-only | Low |
| `ResampledCurve.h/.cpp` | LUT pre-compute + linear interpolation | Medium |
| `CrossfadePair.h` | Complementary pair wrapper, header-only | Low |
| `CurveFactory.h/.cpp` | String → Curve factory | Low |

## Build Integration

The existing `src/CMakeLists.txt` lists sources for `libgradient_motion`. Replace the Phase 0 placeholder `gradient/gradient.cpp` with the new `.cpp` files:

- `gradient/SigmoidCurve.cpp`
- `gradient/BezierCurve.cpp`
- `gradient/ResampledCurve.cpp`
- `gradient/CurveFactory.cpp`

Header-only files don't need to be listed.

CurveFactory depends on `nlohmann/json` (already a project dependency). Add `nlohmann_json::nlohmann_json` to `target_link_libraries` for `gradient_motion` if not already present.

## Testing Approach

Single test binary `tests/test_curves.cpp` covering:

1. **Boundary values**: Every curve type returns 0.0 at t=0.0 and 1.0 at t=1.0
2. **Monotonicity**: Linear, sigmoid, ease-in, ease-out, s-curve produce non-decreasing output for increasing input
3. **ScaledCurve range mapping**: Verify input/output remapping at boundaries and midpoint
4. **ResampledCurve accuracy**: Compare resampled output vs original at many points, verify max deviation < 0.5%
5. **CrossfadePair invariant**: Verify primary + complement = 1.0 at sampled points
6. **CurveFactory**: Verify all type names produce working curves; unknown name falls back to linear
7. **Input clamping**: Verify t < 0 and t > 1 produce boundary values

## Dependencies on Other Modules

None. `gme::gradient` is fully independent. It does not depend on `gme::time`, `gme::signal`, `gme::engine`, `gme::osc`, or any daemon code.

## Who Consumes This Module

- `gme::engine::FadeRegistry` (Phase 4) creates curves via CurveFactory and calls `evaluate()` on each tick
- `gme::engine::ActiveFade` (Phase 4) holds the curve instance for a running fade
