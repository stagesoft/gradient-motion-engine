#include "BezierCurve.h"

namespace gme {
namespace gradient {

BezierCurve::BezierCurve(double cx1, double cy1, double cx2, double cy2)
    : cy1_(cy1), cy2_(cy2)
{
    // cx1 and cx2 are not used in the y-only De Casteljau evaluation;
    // they are accepted to match the documented interface but discarded.
    (void)cx1;
    (void)cx2;
}

double BezierCurve::evaluate(double t) const {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;

    // De Casteljau's algorithm on y-coordinates only.
    // Control points: P0.y=0, P1.y=cy1_, P2.y=cy2_, P3.y=1
    const double p0y = 0.0;
    const double p1y = cy1_;
    const double p2y = cy2_;
    const double p3y = 1.0;

    // First reduction
    double q0y = p0y + t * (p1y - p0y);
    double q1y = p1y + t * (p2y - p1y);
    double q2y = p2y + t * (p3y - p2y);

    // Second reduction
    double r0y = q0y + t * (q1y - q0y);
    double r1y = q1y + t * (q2y - q1y);

    // Final interpolation
    return r0y + t * (r1y - r0y);
}

} // namespace gradient
} // namespace gme
