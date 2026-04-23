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
 * @brief Wrapper that produces a complementary (primary, 1 - primary) pair.
 *
 * CrossfadePair holds a single `Curve` and, on each call to `evaluate()`,
 * returns both the curve's output and its arithmetic complement. The pair
 * always sums to exactly 1.0 — this invariant is guaranteed by computing
 * the complement as `1.0 - primary` from a *single* call to the inner
 * `evaluate()`. Two separate calls would allow floating-point divergence
 * if the curve had any non-deterministic or state-dependent behaviour.
 *
 * @invariant `evaluate(t).primary + evaluate(t).complement == 1.0` for
 *            all t ∈ [0, 1] (and for all clamped out-of-range inputs).
 *
 * @details
 * **Non-inheritance**: CrossfadePair does **not** inherit from `Curve`
 * because it returns two values rather than one `double`. It is a
 * standalone wrapper that holds a `Curve` by ownership.
 *
 * **Ownership**: CrossfadePair takes ownership of its wrapped `Curve` via
 * `std::unique_ptr<Curve>` (NOT `const Curve&`). A reference to a temporary
 * would be a dangling reference — since `CurveFactory::createCurve()` returns
 * a `unique_ptr<Curve>`, ownership transfer is the natural API:
 * @code
 *   auto curve = CurveFactory::createCurve("sigmoid", {});
 *   CrossfadePair pair(std::move(*curve));
 * @endcode
 *
 * @code
 * #include "gradient/CrossfadePair.h"
 * #include "gradient/SigmoidCurve.h"
 *
 * gme::gradient::CrossfadePair xfade(
 *     std::make_unique<gme::gradient::SigmoidCurve>());
 *
 * auto [primary, complement] = xfade.evaluate(0.25);
 * // primary + complement == 1.0, always
 * @endcode
 */
class CrossfadePair {
public:
    /**
     * @brief Result of a crossfade evaluation.
     *
     * @invariant `primary + complement == 1.0` (guaranteed by construction).
     */
    struct Result {
        double primary;    ///< The inner curve's evaluated value.
        double complement; ///< `1.0 - primary`.
    };

    /**
     * @brief Construct a crossfade pair from an owned curve.
     *
     * @param curve Owned inner curve. Must not be null. CrossfadePair takes
     *              exclusive ownership — do not share the pointer.
     */
    explicit CrossfadePair(std::unique_ptr<Curve> curve)
        : curve_(std::move(curve))
    {}

    /**
     * @brief Evaluate the curve and return the complementary pair.
     *
     * @details The inner `evaluate()` is called exactly once, and the
     * complement is computed as `1.0 - primary`. This single-call approach
     * guarantees `primary + complement == 1.0` for all inputs.
     *
     * @param t Normalized progress. Clamped to [0.0, 1.0] by the inner curve.
     * @return `Result` with `primary` (the curve value) and `complement`
     *         (1.0 - primary). The two values sum to exactly 1.0.
     */
    Result evaluate(double t) const {
        double v = curve_->evaluate(t);
        return { v, 1.0 - v };
    }

private:
    std::unique_ptr<Curve> curve_;
};

} // namespace gradient
} // namespace gme
