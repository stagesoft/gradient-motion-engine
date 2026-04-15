#pragma once

#include "Curve.h"

namespace gme {
namespace gradient {

/**
 * @brief Identity curve: output equals input.
 *
 * The simplest possible `Curve` implementation. For every normalized progress
 * value \p t the output is \p t itself — a straight line from (0, 0) to
 * (1, 1). Values outside [0, 1] are clamped before returning.
 *
 * LinearCurve is header-only and stateless. It is the natural fallback when
 * no other curve type is requested, and the recommended inner curve for
 * testing decorators in isolation.
 *
 * @code
 * #include "gradient/LinearCurve.h"
 *
 * gme::gradient::LinearCurve lc;
 * double v = lc.evaluate(0.5);   // returns 0.5
 * double lo = lc.evaluate(-0.1); // clamped → 0.0
 * double hi = lc.evaluate(1.2);  // clamped → 1.0
 * @endcode
 */
class LinearCurve : public Curve {
public:
    /**
     * @brief Evaluate the linear curve at normalized progress \p t.
     * @param t Normalized progress. Clamped to [0.0, 1.0] before evaluation.
     * @return \p t clamped to [0.0, 1.0].
     */
    double evaluate(double t) const override {
        if (t <= 0.0) return 0.0;
        if (t >= 1.0) return 1.0;
        return t;
    }
};

} // namespace gradient
} // namespace gme
