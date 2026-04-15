# Contract: Curve Evaluation Interface

**Module**: `gme::gradient` | **Date**: 2026-04-15

## Abstract Interface: Curve

The single public contract exposed by the gradient module. All curve types, decorators, and the factory return objects conforming to this interface.

### evaluate(t) → value

**Input**: `t` — normalized progress, nominally in [0.0, 1.0]
**Output**: `value` — normalized result in [0.0, 1.0]

**Preconditions**: None (input clamped internally)
**Postconditions**:
- If `t <= 0.0` → returns `evaluate(0.0)` (boundary value, typically 0.0)
- If `t >= 1.0` → returns `evaluate(1.0)` (boundary value, typically 1.0)
- For all concrete types: `evaluate(0.0) = 0.0` and `evaluate(1.0) = 1.0`

**Error conditions**: None — the function is total (defined for all double inputs).

**Thread safety**: Safe to call from a single thread. Not safe for concurrent calls on the same instance without external synchronization (no internal mutable state in most implementations, but not guaranteed for all decorators).

---

## Factory: CurveFactory::createCurve(type, params) → Curve

**Input**:
- `type` — string identifying the curve shape (see supported names in data-model.md)
- `params` — JSON object with curve-specific parameters (missing keys use defaults)

**Output**: Owned curve instance, wrapped in ResampledCurve by default.

**Error handling**:
- Unknown `type` → returns ResampledCurve(LinearCurve) + emits warning
- Malformed `params` → applies defaults for missing/invalid fields, does not throw

---

## CrossfadePair::evaluate(t) → {primary, complement}

**Input**: `t` — normalized progress [0.0, 1.0]
**Output**: Two values where `primary + complement = 1.0` (invariant).

- `primary = inner_curve.evaluate(t)`
- `complement = 1.0 - primary`

---

## Decorator Composition Rules

Decorators can be composed in any order. Typical production chain:

```
CurveFactory("sigmoid", params)
  → SigmoidCurve(steepness, midpoint)
    → ResampledCurve(inner, 256)   // applied by factory
```

Manual composition for range remapping:

```
ScaledCurve(ResampledCurve(SigmoidCurve(...)), in_range, out_range)
```

The Curve interface is the only coupling point — decorators accept any Curve, enabling arbitrary nesting.
