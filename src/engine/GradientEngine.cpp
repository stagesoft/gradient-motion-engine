/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file GradientEngine.cpp
 * @brief GradientEngine implementation — wires MtcTickSource, OscServer,
 *        and MotionRegistry.
 *
 * This file is compiled into the **daemon binary** (gradient-motiond), NOT
 * into libgradient_motion. This allows it to include daemon headers (liblo)
 * without polluting the library's public dependency set.
 *
 * @see src/engine/GradientEngine.h for the public interface.
 */

#include "engine/GradientEngine.h"
#include "daemon/comms/OscServer.h"
#include "logging.h"

namespace gme {
namespace engine {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

GradientEngine::GradientEngine() = default;

GradientEngine::~GradientEngine() {
    shutdown();
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------

bool GradientEngine::initialize(const GradientEngineConfig& config) {
    if (initialized_) return true;

    // --- Build MotionRegistry (status events → logged, no NNG) ---
    registry_ = std::make_unique<gme::motion::MotionRegistry>(
        tickSource_,
        [](gme::signal::StatusKind k,
           const std::string& id,
           const std::string& reason) {
            const char* kind_str = (k == gme::signal::StatusKind::MotionComplete)
                                   ? "MotionComplete" : "MotionError";
            GME_LOG_INFO(std::string("GradientEngine: ") + kind_str +
                         " motion_id=" + id +
                         (reason.empty() ? "" : " reason=" + reason));
        });

    // --- Build OscServer ---
    oscServer_ = std::make_unique<gme::daemon::comms::OscServer>(
        config.oscPort, config.nodeName, &queue_);

    if (!oscServer_->start()) {
        registry_.reset();
        oscServer_.reset();
        return false;
    }

    // --- Register MTC tick callback ---
    tickSource_.setTickCallback([this](long ms) { onTick(ms); });

    // --- Open MIDI port ---
    auto mtcErr = tickSource_.start(config.midiPort);
    if (mtcErr != gme::time::MtcStartError::kOk) {
        tickSource_.setTickCallback({});
        oscServer_->stop();
        registry_.reset();
        oscServer_.reset();
        return false;
    }

    GME_LOG_INFO("GradientEngine initialized: OSC port=" +
                 std::to_string(config.oscPort) + " node=" + config.nodeName);
    initialized_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void GradientEngine::shutdown() {
    if (!initialized_) return;
    initialized_ = false;

    tickSource_.setTickCallback({});

    if (registry_) registry_->cancelAll();

    if (oscServer_) oscServer_->stop();

    registry_.reset();
    oscServer_.reset();
}

// ---------------------------------------------------------------------------
// onTick (MTC callback thread — lock-free, non-blocking)
// ---------------------------------------------------------------------------

void GradientEngine::onTick(long mtc_ms) {
    gme::signal::FadeCommand cmd;
    while (queue_.pop(cmd)) {
        using Type = gme::signal::FadeCommand::Type;
        if (cmd.type == Type::CANCEL_MOTION)
            GME_LOG_DEBUG("GradientEngine: CANCEL_MOTION motion_id=" + cmd.motion_id);
        else if (cmd.type == Type::CANCEL_ALL)
            GME_LOG_DEBUG("GradientEngine: CANCEL_ALL");
        registry_->apply(cmd);
    }

    registry_->tick(mtc_ms);
}

} // namespace engine
} // namespace gme
