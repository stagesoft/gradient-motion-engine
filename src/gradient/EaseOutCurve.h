/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

#pragma once

#include <cmath>
#include "Curve.h"

namespace gme {
namespace gradient {

/**
 * @brief Power-law ease-out curve: output = 1 - (1 - t)^exponent.
 *
 * Produces a curve that starts quickly and decelerates toward the end — the
 * mirror image of EaseInCurve. With the default exponent of 2.0 the shape is
 * a quadratic deceleration. The curve always passes through (0, 0) and
 * (1, 1) regardless of the exponent.
 *
 * EaseOutCurve is header-only. The exponent is fixed at construction time.
 *
 * @code
 * #include "gradient/EaseOutCurve.h"
 *
 * gme::gradient::EaseOutCurve quad;        // default exponent = 2.0
 * double v = quad.evaluate(0.5);           // returns 0.75  (1 - 0.5^2)
 *
 * gme::gradient::EaseOutCurve cubic(3.0);  // cubic ease-out
 * double v3 = cubic.evaluate(0.5);         // returns 0.875 (1 - 0.5^3)
 * @endcode
 */
class EaseOutCurve : public Curve {
public:
    /**
     * @brief Construct an ease-out curve with the given power exponent.
     * @param exponent Power applied to the inverted progress value. Default: 2.0
     *                 (quadratic). Must be > 0 for meaningful results.
     */
    explicit EaseOutCurve(double exponent = 2.0) : exponent_(exponent) {}

    /**
     * @brief Evaluate the ease-out curve at normalized progress \p t.
     * @param t Normalized progress. Clamped to [0.0, 1.0] before evaluation.
     * @return 1 - (1 - t)^exponent, guaranteed in [0.0, 1.0].
     */
    double evaluate(double t) const override {
        if (t <= 0.0) return 0.0;
        if (t >= 1.0) return 1.0;
        return 1.0 - std::pow(1.0 - t, exponent_);
    }

private:
    double exponent_;
};

} // namespace gradient
} // namespace gme
