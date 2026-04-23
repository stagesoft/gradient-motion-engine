/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

#pragma once

#include "Curve.h"

namespace gme {
namespace gradient {

/**
 * @brief Normalized sigmoid (logistic) curve.
 *
 * Produces a smooth S-shaped curve based on the logistic function, normalized
 * so that `evaluate(0.0) == 0.0` and `evaluate(1.0) == 1.0` regardless of
 * the steepness and midpoint parameters.
 *
 * @details
 * The raw logistic function is:
 * @code
 *   raw(t) = 1 / (1 + exp(-steepness * (t - midpoint)))
 * @endcode
 * Because `raw(0)` and `raw(1)` are not exactly 0 and 1, the output is
 * normalized at construction time:
 * @code
 *   offset = raw(0)
 *   scale  = 1.0 / (raw(1) - raw(0))
 *   evaluate(t) = (raw(t) - offset) * scale
 * @endcode
 * This two-point normalization is computed once in the constructor and cached
 * as `norm_offset_` and `norm_scale_`, so no extra cost is paid at
 * evaluation time. The normalization slightly alters the mathematical shape
 * near the endpoints for extreme steepness values, but guarantees exact
 * boundary conditions under all parameter combinations.
 *
 * **Extreme steepness**: With very high steepness (e.g., 50.0) the raw
 * sigmoid saturates almost immediately after 0 and before 1. The
 * normalization still produces exact 0.0 and 1.0 at the boundaries.
 *
 * @code
 * #include "gradient/SigmoidCurve.h"
 *
 * // Default: steepness=8, midpoint=0.5 — gentle S-curve
 * gme::gradient::SigmoidCurve sc;
 * double mid = sc.evaluate(0.5);  // ≈ 0.5 (symmetric midpoint)
 * double lo  = sc.evaluate(0.0);  // exactly 0.0
 * double hi  = sc.evaluate(1.0);  // exactly 1.0
 *
 * // Custom: steeper curve shifted left
 * gme::gradient::SigmoidCurve steep(16.0, 0.3);
 * @endcode
 */
class SigmoidCurve : public Curve {
public:
    /**
     * @brief Construct a normalized sigmoid curve.
     *
     * @param steepness Controls how sharply the curve transitions from 0 to 1.
     *                  Higher values produce a steeper, more step-like curve.
     *                  Default: 8.0. Must be > 0.
     * @param midpoint  The t value at which the raw sigmoid equals 0.5.
     *                  Default: 0.5 (center of the [0, 1] domain).
     */
    explicit SigmoidCurve(double steepness = 8.0, double midpoint = 0.5);

    /**
     * @brief Evaluate the normalized sigmoid at progress \p t.
     * @param t Normalized progress. Clamped to [0.0, 1.0] before evaluation.
     * @return Normalized sigmoid value in [0.0, 1.0].
     */
    double evaluate(double t) const override;

private:
    double steepness_;
    double midpoint_;
    double norm_offset_; ///< raw(0) — subtracted to shift the curve to start at 0
    double norm_scale_;  ///< 1 / (raw(1) - raw(0)) — scales the curve to end at 1

    /// Raw (unnormalized) logistic value at \p t.
    double raw(double t) const;
};

} // namespace gradient
} // namespace gme
