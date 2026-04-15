#pragma once

namespace gme {
namespace gradient {

/**
 * @brief Abstract interface for normalized curve evaluation.
 *
 * A `Curve` maps a normalized progress value \p t in [0.0, 1.0] to a
 * normalized output value in [0.0, 1.0]. This is the single coupling point
 * for the entire `gme::gradient` module: all concrete curve types, decorators
 * (ScaledCurve, ResampledCurve, CrossfadePair), and the CurveFactory return
 * objects that satisfy this interface.
 *
 * **Input clamping**: Every concrete implementation MUST clamp \p t to
 * [0.0, 1.0] before performing its computation. The interface does not
 * perform clamping itself so that subclass implementations retain full
 * control over their evaluation paths without paying an extra branch.
 * Callers may pass any `double`; the contract guarantees the result is
 * defined for all inputs.
 *
 * **Boundary postconditions**: For all concrete types provided by this
 * module, `evaluate(0.0) == 0.0` and `evaluate(1.0) == 1.0`.
 *
 * **Thread safety**: `evaluate()` is safe to call from a single thread.
 * Concurrent calls on the same instance from multiple threads require
 * external synchronisation. No concrete implementation in `gme::gradient`
 * uses mutable state inside `evaluate()`, but this is not part of the
 * interface contract.
 *
 * **Ownership**: Decorators (ScaledCurve, CrossfadePair) take ownership of
 * their wrapped `Curve` via `std::unique_ptr<Curve>`. Copy construction is
 * deleted to prevent accidental slicing; move construction is defaulted to
 * allow `unique_ptr` ownership transfer.
 *
 * @code
 * // Typical usage — instantiate a concrete subclass and call evaluate():
 * #include "gradient/LinearCurve.h"
 *
 * gme::gradient::LinearCurve lc;
 * double mid = lc.evaluate(0.5);  // returns 0.5
 * double clamped = lc.evaluate(-1.0);  // clamped to 0.0 → returns 0.0
 * @endcode
 */
class Curve {
public:
    /**
     * @brief Evaluate the curve at normalized progress \p t.
     *
     * @param t Normalized progress. Nominally in [0.0, 1.0]; values outside
     *          this range MUST be clamped to [0.0, 1.0] by the implementation.
     * @return Normalized output value in [0.0, 1.0].
     */
    virtual double evaluate(double t) const = 0;

    /// Virtual destructor — required for safe polymorphic deletion.
    virtual ~Curve() = default;

    // Copying a polymorphic Curve would silently slice the object.
    // Use std::unique_ptr<Curve> for ownership; clone() if needed.
    Curve(const Curve&)            = delete;
    Curve& operator=(const Curve&) = delete;

    // Move is defaulted so that unique_ptr<Curve> ownership transfer compiles.
    Curve(Curve&&)            = default;
    Curve& operator=(Curve&&) = default;

protected:
    Curve() = default;
};

} // namespace gradient
} // namespace gme
