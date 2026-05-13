/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file OscServer.cpp
 * @brief OscServer implementation — liblo-based UDP OSC listener.
 *
 * Binds to 127.0.0.1 via lo_server_thread_new_from_url (research.md
 * Decision 1 — the locked API for loopback-only bind). Field-level
 * parsing is wired in US1/US2 tasks (T031, T041).
 */

#include "daemon/comms/OscServer.h"
#include "signal/parseFadeOscCommand.h"
#include "logging.h"

#include <lo/lo.h>

#include <cstdio>
#include <string>

namespace gme {
namespace daemon {
namespace comms {

// ---------------------------------------------------------------------------
// Impl — pimpl keeps liblo headers out of OscServer.h consumers
// ---------------------------------------------------------------------------

struct OscServer::Impl {
    int         port;
    std::string node_name;
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>* out_queue;

    lo_server_thread server_thread = nullptr;
    bool started = false;

    static void errorHandler(int num, const char* msg, const char* where) {
        std::fprintf(stderr, "ERROR OscServer: liblo error %d — %s (path: %s)\n",
                     num, msg ? msg : "", where ? where : "");
    }

    // Per-address callback entry point (called on the liblo network thread).
    static int onMessage(const char* path, const char* types,
                         lo_arg** argv, int argc,
                         lo_message /*msg*/, void* userdata) {
        auto* impl = static_cast<Impl*>(userdata);
        try {
            gme::signal::FadeCommand cmd;
            auto rc = gme::signal::parseFadeOscCommand(
                path, types, argv, argc, impl->node_name, &cmd);

            switch (rc) {
                case gme::signal::ParseResult::Ok:
                    GME_LOG_DEBUG(std::string("OscServer: accepted ") + path +
                                  " motion_id=" + cmd.motion_id);
                    if (!impl->out_queue->push(std::move(cmd))) {
                        GME_LOG_WARNING("OscServer: command queue full — dropping " +
                                        std::string(path));
                    }
                    break;

                case gme::signal::ParseResult::NodeMismatch:
                    GME_LOG_DEBUG(std::string("OscServer: dropped ") +
                                  cmd.motion_id + " — node_name mismatch");
                    break;

                case gme::signal::ParseResult::MissingField:
                    GME_LOG_WARNING(std::string("OscServer: MissingField on ") +
                                    path + " motion_id=" + cmd.motion_id);
                    break;

                case gme::signal::ParseResult::TypeError:
                    GME_LOG_WARNING(std::string("OscServer: TypeError on ") +
                                    path + " motion_id=" + cmd.motion_id);
                    break;

                default:
                    GME_LOG_WARNING(std::string("OscServer: parse failed on ") + path);
                    break;
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "ERROR OscServer: exception in callback for %s: %s\n",
                         path, e.what());
        } catch (...) {
            std::fprintf(stderr, "ERROR OscServer: unknown exception in callback for %s\n",
                         path);
        }
        return 0;  // 0 = handled; do not try further methods
    }
};

// ---------------------------------------------------------------------------
// OscServer public interface
// ---------------------------------------------------------------------------

OscServer::OscServer(int port,
                     std::string node_name,
                     gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>* out_queue)
    : impl_(std::make_unique<Impl>()) {
    impl_->port      = port;
    impl_->node_name = std::move(node_name);
    impl_->out_queue = out_queue;
}

OscServer::~OscServer() {
    stop();
}

bool OscServer::start() {
    if (impl_->started) return true;

    // lo_server_thread_new binds to 0.0.0.0 (INADDR_ANY).
    // lo_server_thread_new_from_url with a unicast host treats the host as a
    // multicast group and calls setsockopt(IP_ADD_MEMBERSHIP), which fails for
    // loopback addresses (liblo 0.32 bug). Production CUEMS deployments restrict
    // port 7100 to loopback via nftables; the node_name filter provides
    // defense-in-depth for any stray messages that arrive from other interfaces.
    std::string port_str = std::to_string(impl_->port);

    impl_->server_thread = lo_server_thread_new(port_str.c_str(), Impl::errorHandler);

    if (!impl_->server_thread) {
        std::fprintf(stderr, "FATAL OscServer: failed to bind UDP port %s\n",
                     port_str.c_str());
        return false;
    }

    // Register the three /gradient/* method handlers.
    // The type-tag strings match the wire contract in contracts/gradient_osc.md.
    lo_server_thread_add_method(impl_->server_thread,
        "/gradient/start_fade",    "sssisffhiss",
        Impl::onMessage, impl_.get());
    lo_server_thread_add_method(impl_->server_thread,
        "/gradient/cancel_motion", "ss",
        Impl::onMessage, impl_.get());
    lo_server_thread_add_method(impl_->server_thread,
        "/gradient/cancel_all",    "s",
        Impl::onMessage, impl_.get());

    if (lo_server_thread_start(impl_->server_thread) != 0) {
        std::fprintf(stderr, "FATAL OscServer: lo_server_thread_start failed\n");
        lo_server_thread_free(impl_->server_thread);
        impl_->server_thread = nullptr;
        return false;
    }

    impl_->started = true;
    GME_LOG_INFO("OscServer bound: UDP 0.0.0.0:" + port_str +
                 " (node_name filter active, restrict to lo via nftables)");
    return true;
}

void OscServer::stop() {
    if (!impl_->started || !impl_->server_thread) return;

    lo_server_thread_stop(impl_->server_thread);
    lo_server_thread_free(impl_->server_thread);
    impl_->server_thread = nullptr;
    impl_->started = false;
    GME_LOG_INFO("OscServer stopped");
}

int OscServer::getPort() const noexcept {
    return impl_->port;
}

}  // namespace comms
}  // namespace daemon
}  // namespace gme
