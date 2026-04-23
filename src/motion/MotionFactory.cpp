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
        std::fprintf(stderr, "WARNING MotionFactory: unknown curve type '%s' "
                     "(motion_id=%s)\n", cmd.curve_type.c_str(), cmd.fade_id.c_str());
        ctx.emitStatus(gme::signal::StatusKind::MotionError,
                       cmd.fade_id, "unknown_curve_type");
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
        std::fprintf(stderr, "WARNING MotionFactory: lo_address_new failed for "
                     "%s:%d (motion_id=%s)\n",
                     cmd.osc_host.c_str(), cmd.osc_port, cmd.fade_id.c_str());
        ctx.emitStatus(gme::signal::StatusKind::MotionError,
                       cmd.fade_id, "osc_address_failed");
        return nullptr;
    }

    return std::make_unique<FadeMotion>(
        cmd.fade_id,
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
            std::fprintf(stderr, "INFO MotionFactory: START_CROSSFADE not yet "
                         "implemented (motion_id=%s)\n", cmd.fade_id.c_str());
            return nullptr;

        default:
            return nullptr;
    }
}

} // namespace motion
} // namespace gme
