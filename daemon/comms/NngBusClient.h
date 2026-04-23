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
 *  - a **status worker thread** that pops `StatusEmitRequest` tuples
 *    from the bounded SPSC `statusQueue_` (capacity 64, drop-oldest on
 *    overflow) and calls `sendStatus` — keeping NNG I/O off the MTC
 *    tick thread (FR-006b, FR-007);
 *  - a thread-safe `sendStatus(...)` method used by the drain thread
 *    (direct call) and by the status worker thread;
 *  - a non-blocking `pushStatus(...)` method for the MTC tick thread
 *    to enqueue completion / error notifications.
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
 *  - `pushStatus()` is non-blocking / lock-free (uses `LockFreeQueue`).
 *    Safe to call from the MTC tick thread. On queue full, oldest entry
 *    is dropped with a warning log (FR-007).
 *  - `stop()` sets `running_ = false`, closes the socket (unblocking
 *    any in-flight `nng_recv`), wakes the status worker, and joins all
 *    three threads. Any residual status queue entries are flushed before
 *    the worker exits.
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
#include "signal/StatusEmitRequest.h"

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
 * @brief Daemon-side alias for `gme::signal::StatusKind`.
 *
 * `StatusKind` is defined in `src/signal/StatusEmitRequest.h` so that
 * `libgradient_motion` (specifically `FadeRegistry`) can produce status
 * tuples without depending on any daemon header. This alias keeps all
 * existing `NngBusClient` call sites compiling unchanged.
 */
using StatusKind = gme::signal::StatusKind;

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
     * @brief Push a status request onto the SPSC status worker queue.
     *
     * **Non-blocking and lock-free**. Safe to call from the MTC tick thread
     * (FR-006b). If the 64-slot queue is full the oldest entry is dropped
     * and a warning is logged (FR-007).
     *
     * @param req  Status tuple to enqueue. Moved into the queue.
     *
     * @throws None.
     *
     * @par Example:
     * @code
     *   client.pushStatus({ gme::signal::StatusKind::FadeComplete, fade_id, "" });
     * @endcode
     */
    void pushStatus(gme::signal::StatusEmitRequest&& req);

    /**
     * @brief Observe connection state for diagnostics / tests.
     *
     * @return `true` if the socket currently has an established connection.
     */
    bool isConnected() const noexcept;

private:
    void recvLoop();
    void fallbackDrainLoop();
    void statusWorkerLoop();

    std::string  nodeName_;
    std::string  senderId_;
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>& queue_;
    std::function<void(gme::signal::FadeCommand&)> drainCallback_;

    // Status worker: tick thread pushes into statusQueue_; worker pops + sends.
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> statusQueue_;
    std::thread  statusWorkerThread_;
    std::mutex   statusWorkerCv_mutex_;
    std::condition_variable statusWorkerCv_;

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
