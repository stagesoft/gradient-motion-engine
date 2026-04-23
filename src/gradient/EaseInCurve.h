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
 * @brief Power-law ease-in curve: output = t^exponent.
 *
 * Produces a curve that starts slowly and accelerates toward the end. With
 * the default exponent of 2.0 the shape is a simple quadratic; higher values
 * increase the initial deceleration. The curve always passes through (0, 0)
 * and (1, 1) regardless of the exponent chosen.
 *
 * EaseInCurve is header-only. The exponent is fixed at construction time.
 *
 * @code
 * #include "gradient/EaseInCurve.h"
 *
 * gme::gradient::EaseInCurve quad;         // default exponent = 2.0
 * double v = quad.evaluate(0.5);           // returns 0.25  (0.5^2)
 *
 * gme::gradient::EaseInCurve cubic(3.0);   // cubic ease-in
 * double v3 = cubic.evaluate(0.5);         // returns 0.125 (0.5^3)
 * @endcode
 */
class EaseInCurve : public Curve {
public:
    /**
     * @brief Construct an ease-in curve with the given power exponent.
     * @param exponent Power applied to the progress value. Default: 2.0
     *                 (quadratic). Must be > 0 for meaningful results.
     */
    explicit EaseInCurve(double exponent = 2.0) : exponent_(exponent) {}

    /**
     * @brief Evaluate the ease-in curve at normalized progress \p t.
     * @param t Normalized progress. Clamped to [0.0, 1.0] before evaluation.
     * @return t^exponent, guaranteed in [0.0, 1.0].
     */
    double evaluate(double t) const override {
        if (t <= 0.0) return 0.0;
        if (t >= 1.0) return 1.0;
        return std::pow(t, exponent_);
    }

private:
    double exponent_;
};

} // namespace gradient
} // namespace gme
