/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file FadeMotion.cpp
 * @brief Scalar fade motion implementation.
 */

#include "motion/FadeMotion.h"
#include "osc/OscSender.h"

#include <algorithm>
#include <cmath>

namespace gme {
namespace motion {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

FadeMotion::FadeMotion(std::string mid,
                       std::string osc_k,
                       long  start_mtc,
                       float dur_ms,
                       float sv,
                       float ev,
                       std::unique_ptr<gme::gradient::Curve> curve,
                       lo_address osc_tgt,
                       std::string path,
                       std::string host,
                       int   port,
                       OscSendFn send_fn)
    : start_value(sv)
    , end_value(ev)
    , last_sent_value(sv)
    , osc_path(std::move(path))
    , osc_host(std::move(host))
    , osc_port(port)
    , curve_(std::move(curve))
    , osc_target_(osc_tgt)
    , oscSend_(send_fn ? std::move(send_fn) : OscSendFn(gme::osc::sendFloat))
{
    motion_id    = std::move(mid);
    osc_key      = std::move(osc_k);
    start_mtc_ms = start_mtc;
    duration_ms  = dur_ms;
}

FadeMotion::~FadeMotion() {
    if (osc_target_) {
        lo_address_free(osc_target_);
        osc_target_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// evalAndSend
// ---------------------------------------------------------------------------

EvalResult FadeMotion::evalAndSend(long mtc_ms) {
    // Compute t (FR-001)
    float t;
    if (duration_ms <= 0.0f) {
        t = 1.0f;
    } else {
        float raw = static_cast<float>(mtc_ms - start_mtc_ms) / duration_ms;
        t = std::max(0.0f, std::min(1.0f, raw));
    }

    // Evaluate curve and compute scalar value
    const double curve_val = curve_->evaluate(static_cast<double>(t));
    const float value = start_value
        + (end_value - start_value) * static_cast<float>(curve_val);

    // Send OSC
    const int ret = oscSend_(osc_target_, osc_path.c_str(), value);

    // Update last_sent_value unconditionally (registry removes on failure anyway)
    last_sent_value = value;

    EvalResult r;
    r.completed      = (t >= 1.0f);
    r.failed         = (ret != 0);
    r.failure_reason = r.failed ? "osc_send_failed" : nullptr;
    return r;
}

// ---------------------------------------------------------------------------
// sendSnapToEnd
// ---------------------------------------------------------------------------

void FadeMotion::sendSnapToEnd() {
    if (osc_target_) {
        oscSend_(osc_target_, osc_path.c_str(), end_value);
    }
}

// ---------------------------------------------------------------------------
// inheritFrom
// ---------------------------------------------------------------------------

void FadeMotion::inheritFrom(const IMotion& prior) {
    if (const FadeMotion* fm = dynamic_cast<const FadeMotion*>(&prior)) {
        start_value     = fm->last_sent_value;
        last_sent_value = fm->last_sent_value;
    }
    // Type mismatch (e.g. VectorMotion<N> superseding FadeMotion): no-op.
}

} // namespace motion
} // namespace gme
