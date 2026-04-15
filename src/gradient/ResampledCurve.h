#pragma once

#include <vector>
#include "Curve.h"

namespace gme {
namespace gradient {

/**
 * @brief Lookup-table (LUT) backed curve decorator for constant-time evaluation.
 *
 * ResampledCurve wraps any `Curve` and pre-computes an N-sample table of
 * output values at construction time. Calls to `evaluate()` perform only an
 * array index lookup and a single linear interpolation — no transcendental
 * mathematics at evaluation time.
 *
 * @details
 * **LUT lifecycle**: The source `Curve` is sampled once during construction.
 * The `const Curve&` source reference is used only in the constructor body
 * and is **not retained** after the constructor returns. It is therefore safe
 * to destroy or move the source curve after constructing a `ResampledCurve`.
 *
 * **Memory**: The LUT allocates `samples * sizeof(double)` bytes once at
 * construction (e.g., 256 samples × 8 bytes = 2 KB). No heap allocation
 * occurs during `evaluate()`.
 *
 * **Accuracy**: With the default 256 samples the maximum deviation from the
 * wrapped curve is below 0.5% for all smooth monotone curves provided by
 * this module (SC-002). Accuracy degrades gracefully with fewer samples;
 * even 2 samples (degenerate minimum) produces a valid linear interpolant.
 *
 * **Real-time safety**: `evaluate()` performs only array indexing and
 * arithmetic — no allocations, no locks, no exceptions.
 *
 * @code
 * #include "gradient/ResampledCurve.h"
 * #include "gradient/SigmoidCurve.h"
 *
 * gme::gradient::SigmoidCurve sig(10.0, 0.5);
 * // Source is sampled here; sig can be destroyed afterwards.
 * gme::gradient::ResampledCurve fast(sig, 256);
 *
 * double v = fast.evaluate(0.75);  // pure table-lookup + interpolation
 * @endcode
 */
class ResampledCurve : public Curve {
public:
    /**
     * @brief Construct a LUT-backed copy of the given curve.
     *
     * Samples \p source at `samples` evenly-spaced t values (including
     * 0.0 and 1.0) and stores the results in an internal table. The source
     * curve is not retained after construction.
     *
     * @param source  Any `Curve` to be resampled. Used only during
     *                construction; may be a temporary.
     * @param samples Number of LUT entries. Default: 256. Minimum useful
     *                value: 2 (linear interpolant between the two endpoints).
     */
    ResampledCurve(const Curve& source, int samples = 256);

    /**
     * @brief Evaluate the curve via LUT lookup and linear interpolation.
     *
     * @param t Normalized progress. Clamped to [0.0, 1.0] before lookup.
     * @return Interpolated value from the pre-computed LUT, in [0.0, 1.0].
     */
    double evaluate(double t) const override;

private:
    int                 samples_;
    std::vector<double> lut_;
};

} // namespace gradient
} // namespace gme
