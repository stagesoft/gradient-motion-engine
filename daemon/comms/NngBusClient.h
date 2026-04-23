/**
 * @file NngBusClient.h
 * @brief Daemon-side NNG bus0 client: ingest, filter, parse, enqueue; emit status.
 *
 * `NngBusClient` is the sole component in `gradient-motiond` that knows
 * about NNG and the CUEMS NodeOperation JSON envelope. It owns:
 *
 *  - one `nng_socket` opened in bus0 mode, dialing the Controller URL
 *    in non-blocking mode with reconnect bounds `[500 ms, 5 s]`;
 *  - a background receive thread that runs `nng_recv` → filter →
 *    `parseFadeCommand` → `queue_.push`;
 *  - a 100 ms fallback drain thread that calls `drainOnce()` while
 *    MTC ticks are paused (US3);
 *  - a thread-safe `sendStatus(...)` method used by the fade
 *    subsystem (Phase 4) and by the shutdown handler (FR-013) to
 *    emit `fade_complete` / `fade_error` messages.
 *
 * Lives in `daemon/comms/` — **not** in `libgradient_motion`. Per the
 * project constitution (Principle V, Protocol-Agnostic Core), NNG
 * knowledge is confined to the daemon so the library can be embedded
 * in a non-NNG host (tests, offline tools, third-party apps).
 *
 * ## Threading model
 *
 *  - `start()` creates the receive thread and fallback drain thread.
 *    Safe to call once.
 *  - The receive thread calls `gme::signal::LockFreeQueue::push` on
 *    the referenced queue. That is the only place in this class that
 *    touches the queue on the producer side.
 *  - `drainOnce()` pops all available items calling `drainCallback_`
 *    for each. It is serialised via `drain_in_progress_` against
 *    concurrent MTC tick drains (US3).
 *  - `sendStatus()` is thread-safe — NNG's `nng_send` is safe on a
 *    single socket under NNG 1.10.
 *  - `stop()` sets `running_ = false`, closes the socket (unblocking
 *    any in-flight `nng_recv`), and joins both threads.
 *
 * @par Example:
 * @code
 *   LockFreeQueue<FadeCommand, 64> queue;
 *   NngBusClient client("nodeA", queue);
 *   if (client.start("tcp://127.0.0.1:9093",
 *                    [](FadeCommand& c){ registry.apply(c); })
 *       != StartError::Ok) {
 *       return 1;
 *   }
 *   // ... run loop ...
 *   client.stop();
 * @endcode
 */

#pragma once

#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// Forward-declare nng_socket to avoid pulling <nng/nng.h> into headers.
struct nng_socket_s;
using nng_socket = struct nng_socket_s;

namespace gme {
namespace daemon {
namespace comms {

/**
 * @brief Status kinds emitted on the NNG bus.
 *
 * Serialised into the outbound envelope's `data.event` field.
 *  - `FadeComplete` → `data.event: "fade_complete"` (FR-006a).
 *  - `FadeError`    → `data.event: "fade_error"`    (FR-006b).
 */
enum class StatusKind {
    FadeComplete,
    FadeError
};

/**
 * @brief Outcome of `NngBusClient::start`.
 */
enum class StartError {
    Ok,                 ///< Socket opened and dial issued (non-blocking).
    SocketOpenFailed,   ///< nng_bus0_open returned an error.
    DialFailed          ///< nng_dial returned a fatal error (URL parse, etc.).
};

/**
 * @brief NNG bus0 client for gradient-motiond.
 *
 * See file-level documentation for full contract.
 */
class NngBusClient {
public:
    /**
     * @brief Construct the client (no socket opened yet).
     *
     * @param nodeName  The daemon's own node name.
     * @param queue     Reference to the owner's command queue. Must outlive the client.
     */
    NngBusClient(std::string nodeName,
                 gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>& queue);

    /**
     * @brief Destructor. Calls `stop()` if still running.
     */
    ~NngBusClient();

    NngBusClient(const NngBusClient&)            = delete;
    NngBusClient& operator=(const NngBusClient&) = delete;

    /**
     * @brief Open the NNG socket, dial the hub, start receive + drain threads.
     *
     * @param url           Controller URL, e.g. `tcp://127.0.0.1:9093`.
     * @param drainCallback Callback invoked for each FadeCommand popped by
     *                      the fallback drain timer (and optionally the MTC
     *                      tick site). Stored as `drainCallback_`.
     *
     * @return StartError::Ok on success.
     *
     * @throws None.
     */
    StartError start(const std::string& url,
                     std::function<void(gme::signal::FadeCommand&)> drainCallback);

    /**
     * @brief Stop the receive + drain threads and close the socket.
     *
     * Idempotent. Safe to call from the destructor.
     */
    void stop();

    /**
     * @brief Emit a status message onto the NNG bus.
     *
     * Thread-safe. Blocking I/O — MUST NOT be called from the MTC tick thread.
     *
     * @param kind    `FadeComplete` or `FadeError`.
     * @param fadeId  Fade id included in `data.fade_id`.
     * @param reason  Reason string (only used for FadeError; omitted when empty).
     *
     * @throws None.
     */
    void sendStatus(StatusKind kind,
                    const std::string& fadeId,
                    const std::string& reason = "");

    /**
     * @brief Drain the queue once, serialised against concurrent drain sites.
     *
     * Tries to acquire `drain_in_progress_`; if another site is already
     * draining, returns immediately. Otherwise pops all available items
     * calling `drainCallback_` for each, then releases the flag.
     *
     * @param cb  Callback to invoke per item. Defaults to stored `drainCallback_`
     *            when called without argument (the fallback timer path).
     *
     * @throws None.
     */
    void drainOnce(const std::function<void(gme::signal::FadeCommand&)>& cb);

    /**
     * @brief Observe connection state for diagnostics / tests.
     *
     * @return `true` if the socket currently has an established connection.
     */
    bool isConnected() const noexcept;

private:
    void recvLoop();
    void fallbackDrainLoop();

    std::string  nodeName_;
    std::string  senderId_;
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>& queue_;
    std::function<void(gme::signal::FadeCommand&)> drainCallback_;

    nng_socket*  sock_{nullptr};
    std::thread  recvThread_;
    std::thread  fallbackThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    std::atomic_flag drain_in_progress_ = ATOMIC_FLAG_INIT;
    std::mutex       drainCv_mutex_;
    std::condition_variable drainCv_;
};

} // namespace comms
} // namespace daemon
} // namespace gme
