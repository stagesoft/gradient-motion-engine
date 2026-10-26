/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileContributor: Ion Reguera <ion@stagelab.coop>
 * ***
 */

/**
 * @file MotionFactory.cpp
 * @brief MotionFactory::fromCommand implementation.
 */

#include "motion/MotionFactory.h"
#include "motion/FadeMotion.h"
#include "motion/StartDayAlign.h"
#include "gradient/CurveFactory.h"
#include "osc/OscSender.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "daemon/logging.h"

namespace gme {
namespace motion {

// Day-alignment log state. fromCommand() runs on the tick thread only (the
// registry drains its command queue there), so no lock.
static std::vector<long> s_dayKLogged;
static bool s_asReceivedWarned = false;

static void logStartResolution(StartResolution how, long k, long engineMs, long nowMs,
                               long startMs, const std::string& motionId) {
    if (how == StartResolution::Converted && k != 0 &&
        std::find(s_dayKLogged.begin(), s_dayKLogged.end(), k) == s_dayKLogged.end()) {
        s_dayKLogged.push_back(k);                               // once per k value
        const std::string line = realignLine(k, engineMs, nowMs);
        if (realignIsWarning(k, nowMs)) GME_LOG_WARNING(line);
        else GME_LOG_INFO(line);
    }
    if (how == StartResolution::AsReceived && !s_asReceivedWarned) {
        s_asReceivedWarned = true;
        GME_LOG_WARNING("MTC day-align: fade start " + std::to_string(engineMs)
                        + " ms used as received, MTC head not authoritative yet (motion_id="
                        + motionId + ")");
    }
    if (how != StartResolution::Sentinel && implausibleStart(startMs, nowMs)) {
        GME_LOG_WARNING("MTC day-align: implausible fade start " + std::to_string(startMs)
                        + " ms, MTC " + std::to_string(nowMs) + " ms (motion_id=" + motionId
                        + ") -- wrong MTC day, or an engine time far from now");
    }
}

static std::unique_ptr<IMotion> makeFadeMotion(const gme::signal::FadeCommand& cmd,
                                                const MotionFactory::Context& ctx) {
    // Resolve curve
    const nlohmann::json& params = cmd.curve_params.is_null()
                                   ? nlohmann::json::object()
                                   : cmd.curve_params;
    auto curveOpt = gme::gradient::CurveFactory::createCurve(cmd.curve_type, params);
    if (!curveOpt) {
        GME_LOG_WARNING("MotionFactory: unknown curve type '" + cmd.curve_type
                        + "' (motion_id=" + cmd.motion_id + ")");
        ctx.emitStatus(gme::signal::StatusKind::MotionError,
                       cmd.motion_id, "unknown_curve_type");
        return nullptr;
    }

    // Resolve start_mtc_ms: the -1 sentinel (FR-016) first, never converted; an
    // explicit start is an engine time, converted into this process's MTC day
    // (24h wrap). No clamp: FadeMotion uses the signed difference.
    const long now_ms = ctx.mtcSource.getMtcMs();
    long start_ms = 0, day_k = 0;
    const StartResolution how = resolveStart(
        cmd.start_mtc_ms, now_ms,
        [&ctx](long e, long& l, long& k) { return ctx.mtcSource.toLocalDay(e, l, k); },
        start_ms, day_k);
    logStartResolution(how, day_k, cmd.start_mtc_ms, now_ms, start_ms, cmd.motion_id);

    // Build osc_key
    std::string osc_key = cmd.osc_host + ":"
                          + std::to_string(cmd.osc_port) + ":"
                          + cmd.osc_path;

    // Build lo_address
    lo_address addr = gme::osc::makeAddress(cmd.osc_host, cmd.osc_port);
    if (!addr) {
        GME_LOG_WARNING("MotionFactory: lo_address_new failed for " + cmd.osc_host
                        + ":" + std::to_string(cmd.osc_port)
                        + " (motion_id=" + cmd.motion_id + ")");
        ctx.emitStatus(gme::signal::StatusKind::MotionError,
                       cmd.motion_id, "osc_address_failed");
        return nullptr;
    }

    return std::make_unique<FadeMotion>(
        cmd.motion_id,
        std::move(osc_key),
        start_ms,
        cmd.duration_ms,
        cmd.start_value,
        cmd.end_value,
        std::move(*curveOpt),
        addr,
        cmd.osc_path,
        cmd.osc_host,
        cmd.osc_port,
        ctx.oscSend
    );
}

std::unique_ptr<IMotion> MotionFactory::fromCommand(const gme::signal::FadeCommand& cmd,
                                                     const Context& ctx) {
    using Type = gme::signal::FadeCommand::Type;
    switch (cmd.type) {
        case Type::START_FADE:
            return makeFadeMotion(cmd, ctx);

        case Type::START_CROSSFADE:
            // TODO Phase 7: return makeCrossfadePair(cmd, ctx);
            GME_LOG_INFO("MotionFactory: START_CROSSFADE not yet implemented (motion_id="
                         + cmd.motion_id + ")");
            return nullptr;

        default:
            return nullptr;
    }
}

} // namespace motion
} // namespace gme
