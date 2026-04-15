#pragma once

#include "Curve.h"

namespace gme {
namespace gradient {

/**
 * @brief Smoothstep S-curve: output = 3t² - 2t³.
 *
 * The smoothstep polynomial produces a symmetric S-shaped curve with zero
 * first-derivative at both endpoints — a smooth start and a smooth finish.
 * It is equivalent to Hermite interpolation between 0 and 1 with zero
 * tangents at the endpoints.
 *
 * @details
 * The polynomial is:
 * @code
 *   f(t) = 3t² - 2t³
 * @endcode
 * Properties:
 * - f(0) = 0, f(1) = 1
 * - f'(0) = 0, f'(1) = 0  (zero slope at both ends)
 * - Strictly monotone on [0, 1]
 * - Symmetric: f(1 - t) = 1 - f(t)
 *
 * SCurve is header-only and stateless.
 *
 * @code
 * #include "gradient/SCurve.h"
 *
 * gme::gradient::SCurve sc;
 * double v = sc.evaluate(0.5);  // returns 0.5  (midpoint, symmetric)
 * double v2 = sc.evaluate(0.25); // returns 0.15625
 * @endcode
 */
class SCurve : public Curve {
public:
    /**
     * @brief Evaluate the smoothstep curve at normalized progress \p t.
     *
     * @details Computes 3t² - 2t³ after clamping \p t to [0.0, 1.0].
     *
     * @param t Normalized progress. Clamped to [0.0, 1.0] before evaluation.
     * @return Smoothstep value in [0.0, 1.0].
     */
    double evaluate(double t) const override {
        if (t <= 0.0) return 0.0;
        if (t >= 1.0) return 1.0;
        return 3.0 * t * t - 2.0 * t * t * t;
    }
};

} // namespace gradient
} // namespace gme
