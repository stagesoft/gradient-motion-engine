/**
 * @file test_nng_integration.cpp
 * @brief Real NNG loopback integration tests for NngBusClient.
 *
 * SC-001: NNG-to-queue latency ≤ 5 ms (P99 over 100 frames).
 * SC-003: Drop rate ≤ 1/1000 at 100 cmd/s × 10 s.
 * SC-006: Reconnect within 30 s of disconnect.
 * Outbound check: sendStatus envelope verified end-to-end.
 *
 * Gated behind GME_ENABLE_INTEGRATION_TESTS (set by CMake).
 */

#ifdef GME_ENABLE_INTEGRATION_TESTS

#include <nng/nng.h>
#include <nng/protocol/bus0/bus.h>
#include <nlohmann/json.hpp>

#include "daemon/comms/NngBusClient.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

using gme::signal::FadeCommand;
using gme::signal::LockFreeQueue;
using gme::daemon::comms::NngBusClient;
using gme::daemon::comms::StatusKind;
using gme::daemon::comms::StartError;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL [%s]: %s\n", msg, #cond);            \
            return false;                                                    \
        }                                                                    \
    } while (0)

static std::string make_start_fade_json(const std::string& node_name = "testnode") {
    nlohmann::json env = {
        {"type",   "command"},
        {"action", "apply"},
        {"target", "gradientengine"},
        {"data", {
            {"command",      "start_fade"},
            {"node_name",    node_name},
            {"fade_id",      "fade_it01"},
            {"osc_host",     "127.0.0.1"},
            {"osc_port",     7000},
            {"osc_path",     "/test"},
            {"start_value",  0.0f},
            {"end_value",    1.0f},
            {"duration_ms",  500.0f},
            {"curve_type",   "linear"},
            {"start_mtc_ms", 0}
        }}
    };
    return env.dump();
}

static int hub_listen(nng_socket* hub, const char* url) {
    int rv = nng_bus0_open(hub);
    if (rv != 0) return rv;
    return nng_listen(*hub, url, nullptr, 0);
}

static void hub_close(nng_socket* hub) {
    nng_close(*hub);
}

static int hub_send(nng_socket* hub, const std::string& payload) {
    return nng_send(*hub,
                    const_cast<char*>(payload.data()),
                    payload.size(), 0);
}

// Poll queue until item available or timeout. Returns true if item popped.
static bool queue_poll(LockFreeQueue<FadeCommand, 64>& q, FadeCommand& out,
                       std::chrono::milliseconds timeout) {
    auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        if (q.pop(out)) return true;
        std::this_thread::sleep_for(50us);
    }
    return false;
}

// ---------------------------------------------------------------------------
// SC-001 — Latency ≤ 5 ms (P99 over 100 frames)
// ---------------------------------------------------------------------------

static bool test_sc001_latency() {
    static const char* URL = "inproc://gme-sc001";
    static const int ITERATIONS = 100;

    nng_socket hub;
    CHECK(hub_listen(&hub, URL) == 0, "sc001 hub listen");

    LockFreeQueue<FadeCommand, 64> queue;
    NngBusClient client("testnode", queue);

    // Null drain callback — test thread is sole consumer
    CHECK(client.start(URL, nullptr) == StartError::Ok, "sc001 client start");

    std::this_thread::sleep_for(50ms); // allow connection

    std::string payload = make_start_fade_json();
    std::vector<double> latencies;
    latencies.reserve(ITERATIONS);
    bool all_received = true;

    for (int i = 0; i < ITERATIONS; ++i) {
        FadeCommand cmd;
        auto t0 = Clock::now();
        hub_send(&hub, payload);
        bool got = queue_poll(queue, cmd, 50ms);
        auto t1 = Clock::now();
        if (!got) { all_received = false; break; }
        latencies.push_back(Ms(t1 - t0).count());
    }

    client.stop();
    hub_close(&hub);

    CHECK(all_received, "sc001 all 100 frames received");
    std::sort(latencies.begin(), latencies.end());
    double p99 = latencies[static_cast<size_t>(ITERATIONS * 0.99) - 1];
    std::fprintf(stdout, "  sc001 P99 latency: %.2f ms (limit 5 ms)\n", p99);
    CHECK(p99 <= 5.0, "sc001 P99 latency <= 5 ms");
    return true;
}

// ---------------------------------------------------------------------------
// SC-003 — Drop rate ≤ 1/1000 at 100 cmd/s × 10 s
// ---------------------------------------------------------------------------

static bool test_sc003_drop_rate() {
    static const char* URL = "inproc://gme-sc003";
    static const int TOTAL = 1000;

    nng_socket hub;
    CHECK(hub_listen(&hub, URL) == 0, "sc003 hub listen");

    LockFreeQueue<FadeCommand, 64> queue;

    // Drain callback counts received items
    std::atomic<int> received_count{0};
    auto drain_cb = [&](FadeCommand& /*cmd*/) {
        received_count.fetch_add(1, std::memory_order_relaxed);
    };

    NngBusClient client("testnode", queue);
    CHECK(client.start(URL, drain_cb) == StartError::Ok, "sc003 client start");

    std::this_thread::sleep_for(50ms); // allow connection

    std::string payload = make_start_fade_json();

    // Producer: 100 cmd/s for 10 s
    for (int i = 0; i < TOTAL; ++i) {
        hub_send(&hub, payload);
        std::this_thread::sleep_for(10ms);
    }

    // Wait for all items to be drained (up to 500 ms after last send)
    auto drain_deadline = Clock::now() + 500ms;
    while (Clock::now() < drain_deadline &&
           received_count.load(std::memory_order_acquire) < TOTAL) {
        std::this_thread::sleep_for(50ms);
    }

    int got = received_count.load(std::memory_order_acquire);
    int drops = TOTAL - got;

    client.stop();
    hub_close(&hub);

    std::fprintf(stdout, "  sc003 sent=%d received=%d drops=%d\n", TOTAL, got, drops);
    CHECK(drops <= 1, "sc003 drop rate <= 1/1000");
    return true;
}

// ---------------------------------------------------------------------------
// SC-006 — Reconnect within 30 s of disconnect
// ---------------------------------------------------------------------------

static bool test_sc006_reconnect() {
    static const char* URL = "tcp://127.0.0.1:39093";

    nng_socket hub;
    CHECK(hub_listen(&hub, URL) == 0, "sc006 hub listen");

    LockFreeQueue<FadeCommand, 64> queue;
    std::atomic<int> received{0};
    auto drain_cb = [&](FadeCommand& /*cmd*/) {
        received.fetch_add(1, std::memory_order_relaxed);
    };

    NngBusClient client("testnode", queue);
    CHECK(client.start(URL, drain_cb) == StartError::Ok, "sc006 client start");

    std::this_thread::sleep_for(100ms); // allow initial connection

    // Verify initial connectivity
    std::string payload = make_start_fade_json();
    hub_send(&hub, payload);
    auto deadline = Clock::now() + 2s;
    while (Clock::now() < deadline && received.load() == 0)
        std::this_thread::sleep_for(50ms);
    CHECK(received.load() >= 1, "sc006 initial message received");

    // Disconnect listener
    auto t_disconnect = Clock::now();
    hub_close(&hub);

    // Wait 10 s, then restart listener
    std::this_thread::sleep_for(10s);
    nng_socket hub2;
    CHECK(hub_listen(&hub2, URL) == 0, "sc006 hub2 listen");
    auto t_restart = Clock::now();

    // Wait ≥ 5 s after restart before sending
    std::this_thread::sleep_for(5s);

    // Send a fresh message
    int before = received.load();
    hub_send(&hub2, payload);

    // Assert received within 30 s of the initial disconnect
    auto t_limit = t_disconnect + 30s;
    while (Clock::now() < t_limit && received.load() <= before)
        std::this_thread::sleep_for(100ms);

    double elapsed_ms = Ms(Clock::now() - t_disconnect).count();
    std::fprintf(stdout, "  sc006 reconnect message received %.0f ms after disconnect\n",
                 elapsed_ms);

    client.stop();
    hub_close(&hub2);

    CHECK(received.load() > before, "sc006 message received after reconnect");
    CHECK(elapsed_ms <= 30000.0, "sc006 within 30 s of disconnect");
    (void)t_restart;
    return true;
}

// ---------------------------------------------------------------------------
// Outbound status round-trip
// ---------------------------------------------------------------------------

static bool test_outbound_status() {
    static const char* URL = "inproc://gme-outbound";

    nng_socket hub;
    CHECK(hub_listen(&hub, URL) == 0, "outbound hub listen");

    LockFreeQueue<FadeCommand, 64> queue;
    NngBusClient client("testnode", queue);
    CHECK(client.start(URL, nullptr) == StartError::Ok, "outbound client start");

    std::this_thread::sleep_for(50ms); // allow connection

    // Client sends a status message; hub receives it
    client.sendStatus(StatusKind::FadeComplete, "fade_test1");

    // Poll hub for the status message
    void*  buf  = nullptr;
    size_t sz   = 0;
    nng_socket_set_ms(hub, NNG_OPT_RECVTIMEO, 2000);
    int rv = nng_recv(hub, &buf, &sz, NNG_FLAG_ALLOC);
    CHECK(rv == 0, "outbound hub received status message");

    auto env = nlohmann::json::parse(static_cast<const char*>(buf),
                                      static_cast<const char*>(buf) + sz,
                                      nullptr, false);
    nng_free(buf, sz);

    CHECK(!env.is_discarded(), "outbound status JSON valid");
    CHECK(env.value("target", "") == "gradientengine", "outbound target=gradientengine");
    CHECK(env.contains("data"), "outbound has data");
    CHECK(env["data"].value("event", "") == "fade_complete", "outbound event=fade_complete");
    CHECK(env["data"].value("fade_id", "") == "fade_test1", "outbound fade_id=fade_test1");

    client.stop();
    hub_close(&hub);
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        { "outbound_status",  test_outbound_status  },
        { "sc001_latency",    test_sc001_latency    },
        { "sc003_drop_rate",  test_sc003_drop_rate  },
        { "sc006_reconnect",  test_sc006_reconnect  },
    };

    int failed = 0;
    for (auto& t : tests) {
        std::fprintf(stdout, "--- %s ---\n", t.name);
        bool ok = t.fn();
        std::fprintf(stdout, "%s %s\n\n", ok ? "PASS" : "FAIL", t.name);
        if (!ok) ++failed;
    }

    std::fprintf(stdout, "%d/%zu tests passed\n",
                 (int)(sizeof(tests)/sizeof(tests[0])) - failed,
                 sizeof(tests)/sizeof(tests[0]));
    return failed > 0 ? 1 : 0;
}

#else
// Integration tests not enabled
#include <cstdio>
int main() {
    std::fprintf(stdout, "Integration tests skipped (GME_ENABLE_INTEGRATION_TESTS not set)\n");
    return 0;
}
#endif // GME_ENABLE_INTEGRATION_TESTS
