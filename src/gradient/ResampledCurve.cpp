/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

#include "ResampledCurve.h"
#include <cmath>

namespace gme {
namespace gradient {

ResampledCurve::ResampledCurve(const Curve& source, int samples)
    : samples_(samples), lut_(static_cast<std::size_t>(samples))
{
    for (int i = 0; i < samples_; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(samples_ - 1);
        lut_[static_cast<std::size_t>(i)] = source.evaluate(t);
    }
}

double ResampledCurve::evaluate(double t) const {
    if (t <= 0.0) return lut_.front();
    if (t >= 1.0) return lut_.back();

    double f   = t * static_cast<double>(samples_ - 1);
    int    idx = static_cast<int>(f);

    // Guard against idx reaching the last element (only possible at t==1
    // which is already handled above, but defensive for float rounding).
    if (idx >= samples_ - 1) return lut_.back();

    double frac = f - static_cast<double>(idx);
    return lut_[static_cast<std::size_t>(idx)] * (1.0 - frac)
         + lut_[static_cast<std::size_t>(idx + 1)] * frac;
}

} // namespace gradient
} // namespace gme
