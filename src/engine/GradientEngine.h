/**
 * @file GradientEngine.h
 * @brief Top-level orchestrator wiring MtcTickSource, NngBusClient, and
 *        FadeRegistry into the Phase 4 evaluation pipeline.
 *
 * `GradientEngine` owns all three subsystems and is the single point of
 * initialization and shutdown for the daemon's fade-evaluation core.
 *
 * ## Architecture note
 *
 * `GradientEngine` lives in `src/engine/` (namespace `gme::engine`) but its
 * **implementation** (`GradientEngine.cpp`) is compiled into the daemon binary
 * rather than into `libgradient_motion`. This is necessary because
 * `GradientEngine` owns `NngBusClient` (a daemon-layer component that depends
 * on NNG) while the library must remain embeddable without NNG.
 *
 * Concretely:
 *  - `GradientEngine.h` forward-declares `NngBusClient` — no NNG headers
 *    required by consumers of this header.
 *  - `GradientEngine.cpp` includes `daemon/comms/NngBusClient.h` and is
 *    added to `gradient-motiond` sources in `CMakeLists.txt`.
 *
 * ## Threading model
 *
 *  - `initialize()` and `shutdown()` must be called from the same thread.
 *  - `onTick` fires on the RtMidi MIDI callback thread. It is lock-free
 *    and non-blocking (Constitution Principle IV).
 *  - The NNG recv thread, fallback drain thread, and status worker thread
 *    are owned and managed by `NngBusClient`.
 *
 * @par Example:
 * @code
 *   gme::engine::GradientEngine engine;
 *   if (!engine.initialize({"MTC", "tcp://127.0.0.1:9093", "node1"}))
 *       return 1;
 *   // ... run loop ...
 *   engine.shutdown();
 * @endcode
 */

#pragma once

#include "engine/FadeRegistry.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"
#include "signal/StatusEmitRequest.h"
#include "time/MtcTickSource.h"

#include <memory>
#include <string>

// Forward-declare NngBusClient to avoid pulling daemon headers into
// libgradient_motion. The destructor of GradientEngine is defined in
// GradientEngine.cpp where the full type is available.
namespace gme {
namespace daemon {
namespace comms {
class NngBusClient;
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
    std::string nngUrl;   ///< NNG dial URL (e.g. "tcp://127.0.0.1:9093").
    std::string nodeName; ///< Own node name for NNG message filtering.
};

/**
 * @brief Top-level engine orchestrator.
 *
 * Owns and wires:
 *  - `MtcTickSource` (tick source)
 *  - `LockFreeQueue<FadeCommand, 64>` (command queue: NNG → tick)
 *  - `LockFreeQueue<StatusEmitRequest, 64>` (status queue: tick → NNG worker)
 *  - `NngBusClient` (NNG I/O + status worker thread)
 *  - `FadeRegistry` (evaluation + OSC send)
 */
class GradientEngine {
public:
    GradientEngine();

    /**
     * @brief Destructor. Calls `shutdown()` if still running.
     *
     * Defined in `GradientEngine.cpp` where `NngBusClient` is complete.
     */
    ~GradientEngine();

    GradientEngine(const GradientEngine&)            = delete;
    GradientEngine& operator=(const GradientEngine&) = delete;

    /**
     * @brief Open MIDI port, open NNG socket, start all worker threads.
     *
     * Registers `onTick` as the MTC quarter-frame callback. After this call,
     * `FadeRegistry::tick` fires on every MTC quarter frame.
     *
     * @param config  Engine configuration (MIDI port, NNG URL, node name).
     *
     * @return `true` on success. `false` if MIDI port not found or NNG socket
     *         failed to open. Error details are logged.
     *
     * @throws Never.
     *
     * @par Example:
     * @code
     *   if (!engine.initialize({"MTC", "tcp://127.0.0.1:9093", "node1"}))
     *       return 1;
     * @endcode
     */
    bool initialize(const GradientEngineConfig& config);

    /**
     * @brief Graceful teardown.
     *
     * 1. Deregisters tick callback.
     * 2. Calls `FadeRegistry::cancelAll()` (no final OSC values sent).
     * 3. Calls `NngBusClient::stop()` (joins recv + drain + status worker).
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
     * 1. Try drain: `nngClient_->drainOnce(applyCmd)`.
     * 2. `registry_->tick(mtc_ms)`.
     *
     * @param mtc_ms  Current MTC head position in milliseconds.
     */
    void onTick(long mtc_ms);

    // -----------------------------------------------------------------------
    // Owned subsystems
    // -----------------------------------------------------------------------

    gme::time::MtcTickSource                                        tickSource_;
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>        queue_;
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64>  statusQueue_;
    std::unique_ptr<gme::daemon::comms::NngBusClient>               nngClient_;
    std::unique_ptr<FadeRegistry>                                   registry_;

    bool initialized_ = false;
};

} // namespace engine
} // namespace gme
