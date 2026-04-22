/**
 * @file NngBusClient.h
 * @brief Daemon-side NNG bus0 client: ingest, filter, parse, enqueue; emit status.
 *
 * `NngBusClient` is the sole component in `gradient-motiond` that knows
 * about NNG and the CUEMS NodeOperation JSON envelope. It owns:
 *
 *  - one `nng_socket` opened in bus0 mode, dialing the Controller URL
 *    in non-blocking mode with reconnect bounds `[1 s, 30 s]`;
 *  - a background receive thread that runs `nng_recv` → filter →
 *    `parseFadeCommand` → `queue_.push`;
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
 *  - `start()` creates the receive thread. Safe to call once.
 *  - The receive thread calls `gme::signal::LockFreeQueue::push` on
 *    the referenced queue. That is the only place in this class that
 *    touches the queue.
 *  - `sendStatus()` is thread-safe — NNG's `nng_send` is safe on a
 *    single socket under NNG 1.10. Callers include the NNG recv
 *    thread (on parse error with a parseable fade_id), the MTC tick
 *    thread (Phase 4, on completion), and the shutdown handler
 *    (SIGTERM).
 *  - `stop()` sets `running_ = false`, closes the socket (unblocking
 *    any in-flight `nng_recv`), and joins the thread.
 *
 * ## Shutdown (FR-013)
 *
 * `stop()` does NOT itself emit `fade_error` messages. The shutdown
 * sequence is owned by `GradientEngineApplication`:
 *
 *   1. Acquire the list of active fade ids from `FadeRegistry`
 *      (Phase 4).
 *   2. For each id, call `NngBusClient::sendStatus(kFadeError, id,
 *      "daemon_shutdown")`.
 *   3. Call `NngBusClient::stop()`.
 *   4. Exit within 2 s of the signal.
 *
 * @par Example:
 * @code
 *   LockFreeQueue<FadeCommand, 64> queue;
 *   NngBusClient client("nodeA", queue);
 *   if (client.start("tcp://127.0.0.1:9093") != StartError::Ok) {
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
#include <cstddef>
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
 * Used as the `target` field of outbound status envelopes:
 *  - `FadeComplete` → `target: "fade_complete"` (FR-006a).
 *  - `FadeError`    → `target: "fade_error"`    (FR-006b).
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
     * @param nodeName  The daemon's own node name. Used to filter
     *                  inbound `data.node_name` and to populate the
     *                  `sender` field of outbound status messages
     *                  (`"fadeengine_" + nodeName`).
     * @param queue     Reference to the owner's command queue. The
     *                  receive thread will push `FadeCommand`s here.
     *                  Must outlive the client.
     */
    NngBusClient(std::string nodeName,
                 gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>& queue);

    /**
     * @brief Destructor. Calls `stop()` if still running.
     */
    ~NngBusClient();

    // Non-copyable, non-movable (owns a unique socket + thread).
    NngBusClient(const NngBusClient&)            = delete;
    NngBusClient& operator=(const NngBusClient&) = delete;

    /**
     * @brief Open the NNG socket, dial the hub, start the receive thread.
     *
     * The dial is non-blocking (`NNG_FLAG_NONBLOCK`) with reconnect
     * bounds `min=1 s, max=30 s` configured before the dial. Hub
     * unavailability at startup is not an error — the dial returns
     * `Ok` and NNG will keep trying in the background. The receive
     * thread begins immediately and will deliver commands as soon as
     * the first connection succeeds.
     *
     * @param url  Controller URL, e.g. `tcp://127.0.0.1:9093`.
     *
     * @return StartError::Ok on success; StartError::SocketOpenFailed
     *         if `nng_bus0_open` failed; StartError::DialFailed if
     *         `nng_dial` returned a fatal error (malformed URL, etc.).
     *
     * @throws None.
     *
     * @par Example:
     * @code
     *   if (client.start("tcp://127.0.0.1:9093") != StartError::Ok) {
     *       GME_LOG_ERROR("failed to open NNG socket");
     *       return 1;
     *   }
     * @endcode
     */
    StartError start(const std::string& url);

    /**
     * @brief Stop the receive thread and close the socket.
     *
     * Idempotent. Safe to call from the destructor. After `stop()`
     * returns, `sendStatus()` MUST NOT be called.
     */
    void stop();

    /**
     * @brief Emit a status message onto the NNG bus.
     *
     * Thread-safe. Serialises the JSON envelope on the calling thread
     * and performs a blocking `nng_send`. The socket buffer absorbs
     * the write; no outbound queue is maintained.
     *
     * Envelope shape:
     * ```json
     * {"type":"status","action":"update",
     *  "sender":"fadeengine_<node_name>",
     *  "target":"fade_complete"|"fade_error",
     *  "data":{"fade_id":"<id>","node_name":"<node>"[,"reason":"<text>"]}}
     * ```
     *
     * @param kind    `FadeComplete` or `FadeError`.
     * @param fadeId  Fade id to include in `data.fade_id`.
     * @param reason  Only used when `kind == FadeError`. Free-form
     *                reason string. Defaults to `""` (omitted from
     *                the JSON when empty).
     *
     * @throws None. Send failures are logged and dropped.
     *
     * @par Example:
     * @code
     *   client.sendStatus(StatusKind::FadeComplete, "fade_abc123");
     *   client.sendStatus(StatusKind::FadeError, "fade_def456",
     *                     "daemon_shutdown");
     * @endcode
     */
    void sendStatus(StatusKind kind,
                    const std::string& fadeId,
                    const std::string& reason = "");

    /**
     * @brief Observe connection state for diagnostics / tests.
     *
     * @return `true` if the socket currently has an established
     *         connection to the hub. Advisory only; control flow MUST
     *         NOT depend on this.
     */
    bool isConnected() const noexcept;

private:
    /// Main receive loop. Exits when `running_` goes false.
    void recvLoop();

    std::string  nodeName_;
    std::string  senderId_;   ///< "fadeengine_" + nodeName_.
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>& queue_;

    nng_socket*  sock_{nullptr};  ///< Opaque pointer; real type is `nng_socket` (NNG C API).
    std::thread  recvThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
};

} // namespace comms
} // namespace daemon
} // namespace gme
