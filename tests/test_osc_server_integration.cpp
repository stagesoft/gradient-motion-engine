/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file test_osc_server_integration.cpp
 * @brief Integration test: real OscServer on loopback, liblo client sender,
 *        verify FadeCommand lands in the SPSC queue.
 *
 * Sends one `/gradient/start_fade` message from the same process using
 * `lo_send_from` and confirms:
 *  1. The command is pushed into the queue within 200 ms.
 *  2. `cmd.motion_id` and `cmd.osc_port` match the sent values.
 *
 * Uses port 17100 to avoid colliding with any running daemon.
 */

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

#include <lo/lo.h>

#include "daemon/comms/OscServer.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { \
        std::fprintf(stderr, "FAIL [%s]: %s (line %d)\n", msg, #cond, __LINE__); \
        return false; \
    } } while(0)

static constexpr int kPort = 17100;
static constexpr const char* kNode = "test-node";

static bool test_start_fade_pushes_to_queue() {
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64> queue;
    gme::daemon::comms::OscServer server(kPort, kNode, &queue);

    ASSERT_TRUE(server.start(), "server_start: bind succeeded");

    // Send a start_fade message to the loopback server
    lo_address dest = lo_address_new("127.0.0.1", std::to_string(kPort).c_str());
    ASSERT_TRUE(dest != nullptr, "lo_address: created");

    int sent = lo_send(dest,
        "/gradient/start_fade", "sssisffhiss",
        "motion-itg-1",   // motion_id
        kNode,            // node_name
        "127.0.0.1",      // osc_host
        (int32_t)9001,    // osc_port
        "/vol",           // osc_path
        (float)0.0f,      // start_value
        (float)1.0f,      // end_value
        (int64_t)0,       // start_mtc_ms
        (int32_t)3000,    // duration_ms
        "linear",         // curve_type
        "{}"              // curve_params_json
    );
    ASSERT_TRUE(sent > 0, "lo_send: message sent");
    lo_address_free(dest);

    // Poll the queue for up to 200 ms
    gme::signal::FadeCommand cmd;
    bool received = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < deadline) {
        if (queue.pop(cmd)) {
            received = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    server.stop();

    ASSERT_TRUE(received, "queue: command received within 200 ms");
    ASSERT_TRUE(cmd.type == gme::signal::FadeCommand::Type::START_FADE,
                "cmd: type is START_FADE");
    ASSERT_TRUE(cmd.motion_id == "motion-itg-1", "cmd: motion_id matches");
    ASSERT_TRUE(cmd.osc_port == 9001, "cmd: osc_port matches");
    ASSERT_TRUE(cmd.duration_ms == 3000.0f, "cmd: duration_ms matches");

    return true;
}

static bool test_cancel_all_pushes_to_queue() {
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64> queue;
    gme::daemon::comms::OscServer server(kPort + 2, kNode, &queue);

    ASSERT_TRUE(server.start(), "cancel_all_start: bind succeeded");

    lo_address dest = lo_address_new("127.0.0.1",
                                     std::to_string(kPort + 2).c_str());
    ASSERT_TRUE(dest != nullptr, "cancel_all_addr: created");

    int sent = lo_send(dest, "/gradient/cancel_all", "s", kNode);
    ASSERT_TRUE(sent > 0, "cancel_all_send: message sent");
    lo_address_free(dest);

    gme::signal::FadeCommand cmd;
    bool received = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < deadline) {
        if (queue.pop(cmd)) { received = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    server.stop();

    ASSERT_TRUE(received, "cancel_all: command received within 200 ms");
    ASSERT_TRUE(cmd.type == gme::signal::FadeCommand::Type::CANCEL_ALL,
                "cancel_all: type is CANCEL_ALL");
    return true;
}

static bool test_node_mismatch_drops_message() {
    gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64> queue;
    gme::daemon::comms::OscServer server(kPort + 1, kNode, &queue);

    ASSERT_TRUE(server.start(), "mismatch_start: bind succeeded");

    lo_address dest = lo_address_new("127.0.0.1",
                                     std::to_string(kPort + 1).c_str());
    ASSERT_TRUE(dest != nullptr, "mismatch_addr: created");

    // Send with wrong node_name — should be dropped
    lo_send(dest,
        "/gradient/start_fade", "sssisffhiss",
        "motion-xyz", "wrong-node",
        "127.0.0.1", (int32_t)9001, "/vol",
        (float)0.0f, (float)1.0f,
        (int64_t)0, (int32_t)1000,
        "linear", "{}");
    lo_address_free(dest);

    // Wait and confirm nothing arrived
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.stop();

    gme::signal::FadeCommand cmd;
    ASSERT_TRUE(!queue.pop(cmd), "mismatch: queue empty (message was dropped)");
    return true;
}

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        {"start_fade_pushes_to_queue",  test_start_fade_pushes_to_queue},
        {"cancel_all_pushes_to_queue",  test_cancel_all_pushes_to_queue},
        {"node_mismatch_drops_message", test_node_mismatch_drops_message},
    };

    int passed = 0, failed = 0;
    for (auto& t : tests) {
        bool ok = t.fn();
        if (ok) {
            std::fprintf(stdout, "  PASS  %s\n", t.name);
            ++passed;
        } else {
            std::fprintf(stdout, "  FAIL  %s\n", t.name);
            ++failed;
        }
    }
    std::fprintf(stdout, "\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
