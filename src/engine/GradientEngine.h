/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file GradientEngine.h
 * @brief Top-level orchestrator wiring MtcTickSource, OscServer, and
 *        MotionRegistry into the evaluation pipeline.
 *
 * `GradientEngine` owns all three subsystems and is the single point of
 * initialization and shutdown for the daemon's motion-evaluation core.
 *
 * ## Architecture note
 *
 * `GradientEngine` lives in `src/engine/` (namespace `gme::engine`) but its
 * **implementation** (`GradientEngine.cpp`) is compiled into the daemon binary
 * rather than into `libgradient_motion`. This is necessary because
 * `GradientEngine` owns `OscServer` (a daemon-layer component that depends
 * on liblo) while the library must remain embeddable without daemon headers.
 *
 * Concretely:
 *  - `GradientEngine.h` forward-declares `OscServer` — no liblo headers
 *    required by consumers of this header.
 *  - `GradientEngine.cpp` includes `daemon/comms/OscServer.h` and is
 *    added to `gradient-motiond` sources in `CMakeLists.txt`.
 *
 * ## Threading model
 *
 *  - `initialize()` and `shutdown()` must be called from the same thread.
 *  - `onTick` fires on the RtMidi MIDI callback thread. It is lock-free
 *    and non-blocking (Constitution Principle IV).
 *  - The OscServer's liblo network thread is owned and managed by `OscServer`.
 *
 * @par Example:
 * @code
 *   gme::engine::GradientEngine engine;
 *   if (!engine.initialize({"MTC", 7100, "node1"}))
 *       return 1;
 *   // ... run loop ...
 *   engine.shutdown();
 * @endcode
 */

#pragma once

#include "motion/MotionRegistry.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"
#include "time/MtcTickSource.h"

#include <memory>
#include <string>

// Forward-declare OscServer to avoid pulling daemon headers into
// libgradient_motion. The destructor of GradientEngine is defined in
// GradientEngine.cpp where the full type is available.
namespace gme {
namespace daemon {
namespace comms {
class OscServer;
} // namespace comms
} // namespace daemon
} // namespace gme

namespace gme {
namespace engine {

/**
 * @brief Configuration bundle for `GradientEngine::initialize`.
 */
struct GradientEngineConfig {
    std::string midiPort; ///< MIDI port substring for MtcTickSource (e.g. "MTC").
    int         oscPort;  ///< UDP port for the OSC listener (e.g. 7100).
    std::string nodeName; ///< Own node name for OSC message filtering.
};

/**
 * @brief Top-level engine orchestrator.
 *
 * Owns and wires:
 *  - `MtcTickSource` (tick source)
 *  - `LockFreeQueue<FadeCommand, 64>` (command queue: OscServer → tick)
 *  - `OscServer` (localhost UDP OSC listener)
 *  - `MotionRegistry` (evaluation + transport output)
 */
class GradientEngine {
public:
    GradientEngine();

    /**
     * @brief Destructor. Calls `shutdown()` if still running.
     *
     * Defined in `GradientEngine.cpp` where `OscServer` is complete.
     */
    ~GradientEngine();

    GradientEngine(const GradientEngine&)            = delete;
    GradientEngine& operator=(const GradientEngine&) = delete;

    /**
     * @brief Bind OSC port, open MIDI port, start all worker threads.
     *
     * @param config  Engine configuration (MIDI port, OSC port, node name).
     *
     * @return `true` on success. `false` if MIDI port not found or OSC
     *         socket failed to bind.
     *
     * @throws Never.
     */
    bool initialize(const GradientEngineConfig& config);

    /**
     * @brief Graceful teardown.
     *
     * 1. Deregisters tick callback.
     * 2. Calls `MotionRegistry::cancelAll()` (no final OSC values sent).
     * 3. Calls `OscServer::stop()` (joins liblo network thread).
     *
     * Safe to call more than once (idempotent).
     *
     * @throws Never.
     */
    void shutdown();

private:
    /**
     * @brief Per-tick handler registered with `MtcTickSource`.
     *
     * Fires from the RtMidi MIDI callback thread (lock-free path required).
     *
     * 1. Drain queue_: call `registry_->apply(cmd)` for each command.
     * 2. `registry_->tick(mtc_ms)`.
     *
     * @param mtc_ms  Current MTC head position in milliseconds.
     */
    void onTick(long mtc_ms);

    // -----------------------------------------------------------------------
    // Owned subsystems
    // -----------------------------------------------------------------------

    gme::time::MtcTickSource                                 tickSource_;
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64> queue_;
    std::unique_ptr<gme::daemon::comms::OscServer>           oscServer_;
    std::unique_ptr<gme::motion::MotionRegistry>             registry_;

    bool initialized_ = false;
};

} // namespace engine
} // namespace gme
