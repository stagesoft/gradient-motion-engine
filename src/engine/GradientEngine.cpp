/**
 * @file GradientEngine.cpp
 * @brief GradientEngine implementation — wires MtcTickSource, NngBusClient,
 *        and FadeRegistry.
 *
 * This file is compiled into the **daemon binary** (gradient-motiond), NOT
 * into libgradient_motion. This allows it to include daemon headers (NNG)
 * without polluting the library's public dependency set.
 *
 * @see src/engine/GradientEngine.h for the public interface.
 */

#include "engine/GradientEngine.h"
#include "daemon/comms/NngBusClient.h"

namespace gme {
namespace engine {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

GradientEngine::GradientEngine() = default;

GradientEngine::~GradientEngine() {
    // ~GradientEngine defined here so NngBusClient is a complete type when
    // the unique_ptr destructor is instantiated.
    shutdown();
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------

bool GradientEngine::initialize(const GradientEngineConfig& config) {
    if (initialized_) return true;

    // --- Build NngBusClient ---
    nngClient_ = std::make_unique<gme::daemon::comms::NngBusClient>(
        config.nodeName, queue_);

    // --- Build FadeRegistry ---
    // statusDirect_ routes to NngBusClient::sendStatus (safe from drain thread)
    registry_ = std::make_unique<FadeRegistry>(
        tickSource_,
        statusQueue_,
        [this](gme::signal::StatusKind k,
               const std::string& id,
               const std::string& reason) {
            nngClient_->sendStatus(k, id, reason);
        });

    // --- Start NngBusClient (recv + drain + status worker threads) ---
    auto err = nngClient_->start(
        config.nngUrl,
        [this](gme::signal::FadeCommand& cmd) {
            registry_->apply(cmd);
        });

    if (err != gme::daemon::comms::StartError::Ok) {
        registry_.reset();
        nngClient_.reset();
        return false;
    }

    // --- Register MTC tick callback ---
    tickSource_.setTickCallback([this](long ms) { onTick(ms); });

    // --- Open MIDI port ---
    auto mtcErr = tickSource_.start(config.midiPort);
    if (mtcErr != gme::time::MtcStartError::kOk) {
        tickSource_.setTickCallback({});
        nngClient_->stop();
        registry_.reset();
        nngClient_.reset();
        return false;
    }

    initialized_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void GradientEngine::shutdown() {
    if (!initialized_) return;
    initialized_ = false;

    // 1. Deregister tick callback so no new ticks fire
    tickSource_.setTickCallback({});

    // 2. Cancel all active fades (no final OSC)
    if (registry_) registry_->cancelAll();

    // 3. Stop NNG client (joins recv + drain + status worker threads)
    if (nngClient_) nngClient_->stop();

    registry_.reset();
    nngClient_.reset();
}

// ---------------------------------------------------------------------------
// onTick (MTC callback thread — lock-free, non-blocking)
// ---------------------------------------------------------------------------

void GradientEngine::onTick(long mtc_ms) {
    // Signal to FadeRegistry that we're in tick-thread context so it routes
    // status pushes through the SPSC queue (not direct sendStatus).
    registry_->setTickThreadContext(true);

    // Try to drain the command queue (serialised against fallback drain thread)
    nngClient_->drainOnce([this](gme::signal::FadeCommand& cmd) {
        registry_->apply(cmd);
    });

    // Evaluate all fades + send OSC
    registry_->tick(mtc_ms);

    registry_->setTickThreadContext(false);

    // Push any accumulated StatusEmitRequests from statusQueue_ to NngBusClient.
    // (The NngBusClient status worker thread polls statusQueue_ independently
    //  via pushStatus. We pre-notify it here so it drains promptly.)
    gme::signal::StatusEmitRequest req;
    while (statusQueue_.pop(req)) {
        nngClient_->pushStatus(std::move(req));
    }
}

} // namespace engine
} // namespace gme
