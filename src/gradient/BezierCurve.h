#pragma once

#include "Curve.h"

namespace gme {
namespace gradient {

/**
 * @brief Cubic Bézier curve evaluated via De Casteljau's algorithm.
 *
 * Models a cubic Bézier curve with fixed endpoints P0 = (0, 0) and
 * P3 = (1, 1), and two configurable interior control points P1 and P2.
 * The parameter \p t passed to `evaluate()` maps directly to the Bézier
 * curve parameter — no numerical root-finding is required.
 *
 * @details
 * **De Casteljau evaluation**: Given control points P0, P1, P2, P3 and
 * parameter t ∈ [0, 1], the algorithm recursively blends adjacent points:
 * @code
 *   Q0 = lerp(P0, P1, t)
 *   Q1 = lerp(P1, P2, t)
 *   Q2 = lerp(P2, P3, t)
 *   R0 = lerp(Q0, Q1, t)
 *   R1 = lerp(Q1, Q2, t)
 *   result = lerp(R0, R1, t)
 * @endcode
 * The y-coordinate of the final interpolated point is returned.
 *
 * **Non-monotonicity**: Control points are not constrained to lie within
 * [0, 1]. Placing control points outside this range produces curves that
 * overshoot or loop back. This is intentional — the curve designer is
 * responsible for choosing control points that yield the desired shape.
 * The module evaluates the mathematical curve as-is; monotonicity is not
 * enforced.
 *
 * @code
 * #include "gradient/BezierCurve.h"
 *
 * // Default: gentle ease-in/ease-out cubic
 * gme::gradient::BezierCurve bc;
 * double v = bc.evaluate(0.5);   // ≈ 0.5 with default symmetric control pts
 *
 * // Custom control points for a pronounced ease-in
 * gme::gradient::BezierCurve easeIn(0.9, 0.1, 1.0, 0.9);
 * double v2 = easeIn.evaluate(0.25);
 * @endcode
 */
class BezierCurve : public Curve {
public:
    /**
     * @brief Construct a cubic Bézier curve with the given interior control points.
     *
     * The fixed endpoints are always P0 = (0, 0) and P3 = (1, 1).
     *
     * @param cx1 X coordinate of control point P1. Default: 0.25
     * @param cy1 Y coordinate of control point P1. Default: 0.1
     * @param cx2 X coordinate of control point P2. Default: 0.75
     * @param cy2 Y coordinate of control point P2. Default: 0.9
     */
    explicit BezierCurve(double cx1 = 0.25, double cy1 = 0.1,
                         double cx2 = 0.75, double cy2 = 0.9);

    /**
     * @brief Evaluate the cubic Bézier curve at parameter \p t.
     *
     * @details Uses De Casteljau's algorithm on the y-coordinates of the
     * four control points. The x-coordinates (cx1, cx2) are not used in
     * the y-evaluation — the curve parameter \p t maps directly.
     *
     * @param t Normalized progress. Clamped to [0.0, 1.0] before evaluation.
     * @return Y coordinate of the Bézier curve at \p t. Typically in
     *         [0.0, 1.0] for well-behaved control points; may exceed this
     *         range if control points are outside [0, 1].
     */
    double evaluate(double t) const override;

private:
    double cy1_; ///< Y coordinate of control point P1
    double cy2_; ///< Y coordinate of control point P2
    // P0.y = 0.0 and P3.y = 1.0 are constants; cx1/cx2 unused in y-evaluation.
};

} // namespace gradient
} // namespace gme
