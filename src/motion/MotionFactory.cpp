/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file MotionFactory.cpp
 * @brief MotionFactory::fromCommand implementation.
 */

#include "motion/MotionFactory.h"
#include "motion/FadeMotion.h"
#include "gradient/CurveFactory.h"
#include "osc/OscSender.h"

#include <nlohmann/json.hpp>
#include <cstdio>
#include "daemon/logging.h"

namespace gme {
namespace motion {

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

    // Resolve start_mtc_ms sentinel (FR-016)
    long start_ms = (cmd.start_mtc_ms == -1)
                    ? ctx.mtcSource.getMtcMs()
                    : cmd.start_mtc_ms;

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
