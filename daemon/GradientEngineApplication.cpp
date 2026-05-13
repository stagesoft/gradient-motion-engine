/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file GradientEngineApplication.cpp
 * @brief Implementation of the daemon lifecycle orchestrator.
 */

#include "GradientEngineApplication.h"
#include "config/ConfigurationManager.h"
#include "engine/GradientEngine.h"
#include "logging.h"

#include <csignal>
#include <unistd.h>
#include <iostream>

#ifdef HAVE_CUEMS_LOGGER
#include "cuemslogger/cuemslogger.h"
#endif

// ---------------------------------------------------------------
// Signal handling: file-scope flag bridged to the application
// ---------------------------------------------------------------

static volatile sig_atomic_t g_signalReceived = 0;

static void signalHandler(int /*sig*/) {
    g_signalReceived = 1;
}

// ---------------------------------------------------------------
// GradientEngineApplication implementation
// ---------------------------------------------------------------

GradientEngineApplication::GradientEngineApplication()
    : running_(false)
    , initialized_(false)
    , shutdownComplete_(false)
    , config_(std::make_unique<ConfigurationManager>())
    , engine_(std::make_unique<gme::engine::GradientEngine>())
#ifdef HAVE_CUEMS_LOGGER
    , logger_(nullptr)
#endif
{
}

GradientEngineApplication::~GradientEngineApplication() {
    if (initialized_ && !shutdownComplete_) {
        shutdown();
    }
}

int GradientEngineApplication::initialize(int argc, char** argv) {
    // Parse CLI arguments
    int parseResult = config_->parseArgs(argc, argv);
    if (parseResult != 0) {
        // -1 = help/version (clean exit), >0 = error
        return parseResult;
    }

    // Set log level from CLI argument
    gme_log_set_level(gme_log_level_from_string(config_->getLogLevel()));

#ifdef HAVE_CUEMS_LOGGER
    // Instantiate CuemsLogger singleton with "GradientEngine" slug
    // This opens syslog with identifier "Cuems:GradientEngine"
    logger_ = new CuemsLogger("GradientEngine");
#endif

    // Log startup information
    GME_LOG_INFO("gradient-motiond starting");
    GME_LOG_INFO("  MIDI port : " + config_->getMidiPort());
    GME_LOG_INFO("  OSC port  : " + std::to_string(config_->getGradientOscPort()));
    GME_LOG_INFO("  Node name : " + config_->getNodeName());
    GME_LOG_INFO("  Log level : " + config_->getLogLevel());
    GME_LOG_INFO("  Conf path : " + config_->getConfPath());

    // Initialize GradientEngine (opens MIDI port and OSC socket)
    gme::engine::GradientEngineConfig engCfg;
    engCfg.midiPort = config_->getMidiPort();
    engCfg.oscPort  = config_->getGradientOscPort();
    engCfg.nodeName = config_->getNodeName();

    if (!engine_->initialize(engCfg)) {
        GME_LOG_ERROR("gradient-motiond: GradientEngine initialization failed");
        return 1;
    }

    // Install signal handlers
    installSignalHandlers();

    initialized_ = true;
    return 0;
}

int GradientEngineApplication::run() {
    running_ = true;
    GME_LOG_INFO("gradient-motiond running — waiting for signal");

    // Main blocking loop — pause() returns when any signal is delivered
    while (running_ && !g_signalReceived) {
        pause();
    }

    running_ = false;
    shutdown();
    return 0;
}

void GradientEngineApplication::shutdown() {
    if (shutdownComplete_) return;

    GME_LOG_INFO("gradient-motiond shutting down");

    // Gracefully stop the engine (cancels fades, joins OSC server thread)
    if (engine_) {
        engine_->shutdown();
        engine_.reset();
    }

    config_.reset();

#ifdef HAVE_CUEMS_LOGGER
    delete logger_;
    logger_ = nullptr;
#endif

    shutdownComplete_ = true;
}

void GradientEngineApplication::installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);
}
