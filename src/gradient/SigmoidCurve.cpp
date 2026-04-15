#include "SigmoidCurve.h"
#include <cmath>

namespace gme {
namespace gradient {

SigmoidCurve::SigmoidCurve(double steepness, double midpoint)
    : steepness_(steepness), midpoint_(midpoint)
{
    // Pre-compute normalization constants so evaluate() pays no extra cost.
    norm_offset_ = raw(0.0);
    double r1    = raw(1.0);
    norm_scale_  = 1.0 / (r1 - norm_offset_);
}

double SigmoidCurve::raw(double t) const {
    return 1.0 / (1.0 + std::exp(-steepness_ * (t - midpoint_)));
}

double SigmoidCurve::evaluate(double t) const {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    return (raw(t) - norm_offset_) * norm_scale_;
}

} // namespace gradient
} // namespace gme
