/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

#pragma once

#include <memory>
#include <string>

#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"

namespace gme {
namespace daemon {
namespace comms {

/**
 * @file OscServer.h
 * @brief Localhost UDP OSC listener feeding the gradient-motiond tick loop.
 *
 * Owns one liblo server thread bound to `127.0.0.1` only (never a routable
 * interface). Accepts the three `/gradient/\*` addresses defined in the
 * wire contract, parses them via `parseFadeOscCommand`, and on `ParseResult::Ok`
 * pushes to the `LockFreeQueue<FadeCommand, 64>` supplied at construction.
 *
 * Namespace: `gme::daemon::comms` (matches NngBusClient convention).
 *
 * ## Threading
 *
 * - One liblo server thread owned by this object.
 * - Callbacks are wait-free with respect to the tick thread (SPSC push).
 * - `start()` / `stop()` must be called from the daemon's main thread.
 *
 * @par Example:
 * @code
 *   gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64> queue;
 *   gme::daemon::comms::OscServer server(7100, "node-002", &queue);
 *   if (!server.start()) return 1;  // bind failed
 *   // ... run loop ...
 *   server.stop();
 * @endcode
 */
class OscServer {
public:
    /**
     * @brief Construct the server (does not bind yet).
     *
     * @param port       UDP port to listen on. 1–65535.
     * @param node_name  Local node name for the node_name filter.
     * @param out_queue  Non-owning pointer to the command queue. Must outlive.
     */
    OscServer(int port,
              std::string node_name,
              gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>* out_queue);

    /**
     * @brief Destructor. Calls stop() if start() was called.
     */
    ~OscServer();

    OscServer(const OscServer&)            = delete;
    OscServer& operator=(const OscServer&) = delete;
    OscServer(OscServer&&)                 = delete;
    OscServer& operator=(OscServer&&)      = delete;

    /**
     * @brief Bind the socket and start the network thread.
     *
     * @return true on success, false if the bind failed.
     */
    bool start();

    /**
     * @brief Stop the network thread and free the liblo server. Idempotent.
     */
    void stop();

    /** @brief Return the bound UDP port. */
    int getPort() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace comms
}  // namespace daemon
}  // namespace gme
