/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 *
 * Contract header for daemon/comms/OscServer (Phase H — feature 007).
 * Authoritative shape for the implementation. The final installed file lives
 * at daemon/comms/OscServer.h with the same public surface.
 */

#pragma once

#include <lo/lo.h>

#include <atomic>
#include <memory>
#include <string>

#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"

namespace gme {
namespace daemon {
namespace comms {

/**
 * @file OscServer.h
 * @brief Localhost UDP OSC listener that feeds the gradient-motiond tick loop.
 *
 * `OscServer` owns one liblo network thread and accepts the three OSC
 * addresses defined in
 * [`specs/007-osc-input-transport/contracts/gradient_osc.md`](../specs/007-osc-input-transport/contracts/gradient_osc.md):
 * `/gradient/start_fade`, `/gradient/cancel_motion`, `/gradient/cancel_all`.
 *
 * The server runs on `127.0.0.1` only — it never binds to a routable
 * interface. Every accepted message is parsed via
 * `gme::signal::parseFadeOscCommand` and, on `ParseResult::Ok`, pushed to
 * a non-owning `LockFreeQueue<FadeCommand, 64>*` supplied at construction.
 * The queue is drained by the MTC tick thread (`gme::engine::GradientEngine`).
 *
 * ## Threading
 *
 * - One liblo server thread owned by this object. Started by `start()`,
 *   joined by `stop()` and the destructor.
 * - Per-address callbacks run on that thread. They are wait-free with
 *   respect to the tick thread (single-producer push into the
 *   `LockFreeQueue`).
 * - `start()` and `stop()` are NOT thread-safe relative to each other.
 *   The daemon's main thread is the sole driver.
 *
 * ## Real-time safety
 *
 * Constitution Principle IV (Real-Time Safety) applies on the tick-thread
 * side only — the OSC server thread is the *producer*. Allocations in the
 * server thread (e.g. `nlohmann::json::parse` for `curve_params_json`) are
 * tolerated; they do not affect the tick path.
 *
 * ## Lifetime
 *
 * Constructor binds the UDP socket and registers method callbacks. `start()`
 * spawns the network thread. `stop()` requests teardown and joins. The
 * destructor calls `stop()` if not already stopped. Copy/move disabled.
 *
 * ## Error semantics
 *
 * - Bind failure (port in use, permission denied) — `start()` returns false
 *   and logs the OS errno via the daemon's logger. The caller (daemon main)
 *   treats this as a fatal startup error.
 * - Method-callback exceptions are caught at the C ABI boundary (liblo is C,
 *   so the C++ callbacks must not let exceptions escape). The implementation
 *   wraps each callback body in a try/catch and logs uncaught exceptions
 *   without abort.
 *
 * @par Example usage:
 * @code
 *   gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64> queue;
 *   gme::daemon::comms::OscServer server(7100, "node-002", &queue);
 *   if (!server.start()) {
 *       // bind failed — abort startup
 *       return 1;
 *   }
 *   // ... run tick loop, draining `queue` ...
 *   server.stop();  // or let the destructor handle it
 * @endcode
 */
class OscServer {
public:
    /**
     * @brief Construct the server (does not bind yet).
     *
     * @param port       UDP port to listen on. 1–65535. Caller is responsible
     *                   for ensuring the port is free.
     * @param node_name  Local node name used by the parser as the
     *                   defense-in-depth `node_name` filter. Non-empty.
     * @param out_queue  Non-owning pointer to the queue that the tick thread
     *                   drains. Must outlive this `OscServer`. Must NOT be
     *                   `nullptr`.
     *
     * @note No I/O happens in the constructor. Failure to bind is reported
     *       by `start()` rather than by throwing.
     */
    OscServer(int port,
              std::string node_name,
              gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>* out_queue);

    /**
     * @brief Destructor. Calls `stop()` if `start()` was called.
     *
     * Safe to call from the daemon's shutdown handler.
     */
    ~OscServer();

    OscServer(const OscServer&) = delete;
    OscServer& operator=(const OscServer&) = delete;
    OscServer(OscServer&&) = delete;
    OscServer& operator=(OscServer&&) = delete;

    /**
     * @brief Bind the socket and start the network thread.
     *
     * @return `true` on success, `false` if the bind failed (port in use,
     *         permission denied, etc.). On failure, the server is left in
     *         a not-started state and `stop()` is a no-op.
     *
     * @note Bind is restricted to `127.0.0.1`. Validated at implementation
     *       time per research.md Decision 1 Open Item 1.
     */
    bool start();

    /**
     * @brief Stop the network thread and free the liblo server. Idempotent.
     *
     * Blocks until the network thread has joined. Bounded by the daemon-wide
     * 2-second shutdown budget (Constitution Principle IV).
     */
    void stop();

    /**
     * @brief Return the bound UDP port.
     *
     * Same as the value passed to the constructor; provided for symmetry
     * with implementations that may pick an ephemeral port (e.g. tests
     * passing `0` — not used in production).
     */
    int getPort() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace comms
}  // namespace daemon
}  // namespace gme
