# Data Model: Gradient Curves Module

**Branch**: `002-gradient-curves` | **Date**: 2026-04-15

## Entities

### Curve (abstract interface)

The evaluation contract. All concrete types and decorators implement this interface.

| Attribute | Type | Description |
| --------- | ---- | ----------- |
| (none) | — | Pure interface, no stored state |

**Operations**:
- `evaluate(t: double) → double` — Maps normalized progress [0,1] to normalized output [0,1]. Input clamped to [0,1] before evaluation.

**Lifecycle**: Created once per fade via CurveFactory. Evaluated many times during fade lifetime. Destroyed when fade completes or is cancelled.

---

### LinearCurve

| Attribute | Type | Description |
| --------- | ---- | ----------- |
| (none) | — | Stateless: output = input |

---

### SigmoidCurve

| Attribute | Type | Default | Description |
| --------- | ---- | ------- | ----------- |
| steepness | double | 8.0 | Controls slope at midpoint (higher = sharper transition) |
| midpoint | double | 0.5 | Progress value where curve is at 50% output |
| norm_offset | double | (computed) | `raw(0)` — cached at construction for normalization |
| norm_scale | double | (computed) | `1 / (raw(1) - raw(0))` — cached at construction |

**Invariant**: `evaluate(0) = 0`, `evaluate(1) = 1` guaranteed by normalization regardless of steepness/midpoint.

---

### BezierCurve

| Attribute | Type | Default | Description |
| --------- | ---- | ------- | ----------- |
| cx1 | double | 0.25 | First control point x (progress axis) |
| cy1 | double | 0.1 | First control point y (value axis) |
| cx2 | double | 0.75 | Second control point x |
| cy2 | double | 0.9 | Second control point y |

**Fixed endpoints**: P0 = (0, 0), P3 = (1, 1). Control points P1 = (cx1, cy1), P2 = (cx2, cy2).

**Note**: Parametric evaluation — `t` is the bezier parameter, not the x-axis value. Non-monotonic control points are permitted (designer's responsibility).

---

### EaseInCurve

| Attribute | Type | Default | Description |
| --------- | ---- | ------- | ----------- |
| exponent | double | 2.0 | Power curve exponent |

**Formula**: `output = t^exponent`

---

### EaseOutCurve

| Attribute | Type | Default | Description |
| --------- | ---- | ------- | ----------- |
| exponent | double | 2.0 | Power curve exponent |

**Formula**: `output = 1 - (1-t)^exponent`

---

### SCurve

| Attribute | Type | Description |
| --------- | ---- | ----------- |
| (none) | — | Stateless: smoothstep polynomial |

**Formula**: `output = 3t^2 - 2t^3`

---

### ScaledCurve (decorator)

| Attribute | Type | Default | Description |
| --------- | ---- | ------- | ----------- |
| inner | Curve | (required) | The wrapped curve instance |
| in_min | double | 0.0 | Input range minimum |
| in_max | double | 1.0 | Input range maximum |
| out_min | double | 0.0 | Output range minimum |
| out_max | double | 1.0 | Output range maximum |

**Mapping**: `normalized_t = (input - in_min) / (in_max - in_min)` → `inner.evaluate(normalized_t)` → `out_min + result * (out_max - out_min)`

---

### ResampledCurve (decorator)

| Attribute | Type | Default | Description |
| --------- | ---- | ------- | ----------- |
| inner | Curve | (required) | The source curve (used only at construction) |
| samples | int | 256 | Number of pre-computed LUT entries |
| lut | double[] | (computed) | Pre-computed values at uniform intervals |

**Lifecycle**: At construction, evaluates `inner` at `samples` uniform points to fill `lut`. After construction, `inner` is no longer needed. `evaluate()` performs index lookup + linear interpolation into `lut`.

**Memory**: `samples × 8 bytes` (e.g., 256 × 8 = 2 KB)

---

### CrossfadePair (wrapper)

| Attribute | Type | Description |
| --------- | ---- | ----------- |
| curve | Curve | The underlying curve for the primary value |

**Operations**:
- `evaluate(t) → {primary, complement}` — Returns `{curve.evaluate(t), 1.0 - curve.evaluate(t)}`

**Invariant**: `primary + complement = 1.0` at all points, by construction.

---

### CurveFactory (static)

No stored state. Maps string names to curve constructors.

**Supported type names**:

| Name | Curve Type | Parameters |
| ---- | ---------- | ---------- |
| `"linear"` | LinearCurve | (none) |
| `"sigmoid"` | SigmoidCurve | `steepness`, `midpoint` |
| `"bezier"` | BezierCurve | `cx1`, `cy1`, `cx2`, `cy2` |
| `"ease_in"` | EaseInCurve | `exponent` |
| `"ease_out"` | EaseOutCurve | `exponent` |
| `"scurve"` | SCurve | (none) |

**Behavior**: Returns a ResampledCurve wrapping the requested type by default. Unknown names produce a warning + LinearCurve fallback.

## Relationships

```
CurveFactory ──creates──▶ Curve (concrete)
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
             ResampledCurve       ScaledCurve
              (wraps Curve)      (wraps Curve)
                    
CrossfadePair ──holds──▶ Curve
```

- CurveFactory produces concrete curves, wrapped in ResampledCurve by default
- ScaledCurve and ResampledCurve are decorators — they wrap any Curve instance
- CrossfadePair holds one Curve and derives complementary output
- Decorators are composable: `ScaledCurve(ResampledCurve(SigmoidCurve(...)))` is valid
