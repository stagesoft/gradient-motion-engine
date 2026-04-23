/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

#pragma once

#include <memory>
#include "Curve.h"

namespace gme {
namespace gradient {

/**
 * @brief Decorator that remaps a curve's input and output value ranges.
 *
 * ScaledCurve wraps any `Curve` and applies configurable input and output
 * range remapping:
 * - The raw input value is normalized from [in_min, in_max] to [0, 1]
 *   before being passed to the inner curve.
 * - The inner curve's [0, 1] output is then mapped to [out_min, out_max].
 *
 * @details
 * **Mapping formulas**:
 * @code
 *   norm_t = (t - in_min) / (in_max - in_min)   // map input to [0,1]
 *   norm_t = clamp(norm_t, 0.0, 1.0)
 *   result = inner->evaluate(norm_t)              // evaluate inner curve
 *   output = out_min + result * (out_max - out_min) // map output to range
 * @endcode
 *
 * **Ownership**: ScaledCurve takes ownership of its inner `Curve` via
 * `std::unique_ptr<Curve>` (NOT `const Curve&`). A reference member to a
 * temporary would be a dangling reference after the constructor returns —
 * for example, in the decorator composition pattern:
 * @code
 *   ScaledCurve(std::make_unique<ResampledCurve>(SigmoidCurve(...)), ...)
 * @endcode
 * the intermediate `ResampledCurve` is a heap-allocated temporary whose
 * lifetime must extend beyond the ScaledCurve's lifetime. Ownership
 * transfer via `unique_ptr` ensures this automatically.
 *
 * @code
 * #include "gradient/ScaledCurve.h"
 * #include "gradient/LinearCurve.h"
 *
 * // Remap output to [0.0, 0.5]: a full [0,1] progress fades to half volume
 * gme::gradient::ScaledCurve half(
 *     std::make_unique<gme::gradient::LinearCurve>(),
 *     0.0, 1.0,   // input range  [0, 1] (identity)
 *     0.0, 0.5    // output range [0, 0.5]
 * );
 * double v = half.evaluate(1.0);  // returns 0.5
 *
 * // Partial fade: input range [0.3, 0.8], map to full output [0, 1]
 * gme::gradient::ScaledCurve partial(
 *     std::make_unique<gme::gradient::LinearCurve>(),
 *     0.3, 0.8,
 *     0.0, 1.0
 * );
 * double v2 = partial.evaluate(0.55);  // midpoint → 0.5
 * @endcode
 */
class ScaledCurve : public Curve {
public:
    /**
     * @brief Construct a range-remapping decorator.
     *
     * @param inner    Owned inner curve. Must not be null. ScaledCurve takes
     *                 exclusive ownership — do not share the pointer.
     * @param in_min   Lower bound of the input range. Default: 0.0
     * @param in_max   Upper bound of the input range. Default: 1.0
     * @param out_min  Lower bound of the output range. Default: 0.0
     * @param out_max  Upper bound of the output range. Default: 1.0
     */
    explicit ScaledCurve(std::unique_ptr<Curve> inner,
                         double in_min  = 0.0, double in_max  = 1.0,
                         double out_min = 0.0, double out_max = 1.0)
        : inner_(std::move(inner))
        , in_min_(in_min),   in_max_(in_max)
        , out_min_(out_min), out_max_(out_max)
    {}

    /**
     * @brief Evaluate with input/output range remapping.
     *
     * @param t Raw input value. Remapped from [in_min, in_max] to [0, 1],
     *          clamped, passed to the inner curve, then output is remapped
     *          from [0, 1] to [out_min, out_max].
     * @return Remapped output value.
     */
    double evaluate(double t) const override {
        double norm = (t - in_min_) / (in_max_ - in_min_);
        if (norm <= 0.0) norm = 0.0;
        if (norm >= 1.0) norm = 1.0;
        double result = inner_->evaluate(norm);
        return out_min_ + result * (out_max_ - out_min_);
    }

private:
    std::unique_ptr<Curve> inner_;
    double in_min_;
    double in_max_;
    double out_min_;
    double out_max_;
};

} // namespace gradient
} // namespace gme
