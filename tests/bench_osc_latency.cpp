/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file bench_osc_latency.cpp
 * @brief Two-mode OSC loopback latency benchmark — spec SC-002.
 *
 * ## Mode A — synchronous lo_server
 *
 * Uses `lo_server` (non-threaded). After each `lo_send` the benchmark calls
 * `lo_server_recv` which dispatches the callback synchronously on the same
 * thread.  The timestamp is taken inside the callback immediately after
 * `LockFreeQueue::push` returns, so the measurement is:
 *
 *   latency = t_push − t_send
 *
 * This captures pure UDP loopback RTT + liblo unmarshalling + queue push,
 * with no OS thread-scheduling noise.
 *
 * Spec SC-002 target: p50 ≤ 1 ms, p99 ≤ 5 ms.
 *
 * ## Mode B — lo_server_thread via production OscServer
 *
 * Sends `/gradient/cancel_all` through the real OscServer (same code path
 * as production).  The background server thread wakes, parses the message
 * via `parseFadeOscCommand`, and pushes to the queue.  The benchmark
 * busy-polls `queue.pop` and records:
 *
 *   latency ≈ t_pop − t_send
 *
 * The busy-poll overhead (a handful of nanoseconds) is negligible relative
 * to thread wake-up time.
 *
 * Production-grade targets (conservative, calibrated on dev host; tighten
 * after measuring on node-002 hardware): p50 ≤ 5 ms, p99 ≤ 20 ms.
 *
 * ## Output
 *
 * Results are printed to stdout and appended to BENCH_RESULTS_FILE (defined
 * at compile time by CMake as an absolute path into the source tree).
 *
 * Usage: bench_osc_latency [N]   (default N = 1000, warmup = 200)
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <lo/lo.h>

#include "daemon/comms/OscServer.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"

// ---------------------------------------------------------------------------
// Calibrated thresholds
// ---------------------------------------------------------------------------

static constexpr long long kSyncP50Ns   =  1'000'000LL;  // 1 ms  (spec SC-002)
static constexpr long long kSyncP99Ns   =  5'000'000LL;  // 5 ms  (spec SC-002)
// Production targets — calibrated from dev-host run (p50≈0.012 ms, p99≈0.022 ms);
// tighten after measuring on node-002 hardware if warranted:
static constexpr long long kThreadP50Ns =  1'000'000LL;  // 1 ms  (matches SC-002; 83x headroom)
static constexpr long long kThreadP99Ns =  5'000'000LL;  // 5 ms  (matches SC-002; 225x headroom)

static constexpr int kPortSync   = 19200;
static constexpr int kPortThread = 19201;
static constexpr int kWarmup     = 200;

using Clock = std::chrono::steady_clock;
using Ns    = std::chrono::nanoseconds;
using LFQ   = gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>;

// ---------------------------------------------------------------------------
// Mode A helpers
// ---------------------------------------------------------------------------

struct SyncCtx {
    LFQ*             queue;
    Clock::time_point t_push;
};

static int syncCallback(const char*, const char*, lo_arg**, int,
                        lo_message, void* user) {
    auto* ctx = static_cast<SyncCtx*>(user);
    gme::signal::FadeCommand cmd;
    cmd.type = gme::signal::FadeCommand::Type::CANCEL_ALL;
    ctx->queue->push(std::move(cmd));
    ctx->t_push = Clock::now();
    return 0;
}

static std::vector<long long> runSyncMode(int n) {
    LFQ      queue;
    SyncCtx  ctx{&queue, {}};

    std::string ps = std::to_string(kPortSync);
    lo_server  srv  = lo_server_new(ps.c_str(), nullptr);
    lo_server_add_method(srv, "/bench/ping", "s", syncCallback, &ctx);
    lo_address dest = lo_address_new("127.0.0.1", ps.c_str());

    // Warmup (drain queue after each recv to keep it below capacity)
    gme::signal::FadeCommand dummy;
    for (int i = 0; i < kWarmup; i++) {
        lo_send(dest, "/bench/ping", "s", "w");
        lo_server_recv_noblock(srv, 20);
        while (queue.pop(dummy)) {}
    }

    std::vector<long long> lat;
    lat.reserve(n);

    for (int i = 0; i < n; i++) {
        auto t_send = Clock::now();
        lo_send(dest, "/bench/ping", "s", "x");
        lo_server_recv_noblock(srv, 50);
        lat.push_back(std::chrono::duration_cast<Ns>(ctx.t_push - t_send).count());
        // drain to prevent queue fill on the next iteration
        while (queue.pop(dummy)) {}
    }

    lo_address_free(dest);
    lo_server_free(srv);
    return lat;
}

// ---------------------------------------------------------------------------
// Mode B helpers
// ---------------------------------------------------------------------------

static std::vector<long long> runThreadMode(int n) {
    LFQ queue;
    gme::daemon::comms::OscServer server(kPortThread, "bench-node", &queue);
    if (!server.start()) {
        std::fprintf(stderr, "FATAL bench_osc_latency: OscServer start failed (port %d)\n",
                     kPortThread);
        return {};
    }

    std::string ps = std::to_string(kPortThread);
    lo_address dest = lo_address_new("127.0.0.1", ps.c_str());

    // Warmup
    gme::signal::FadeCommand dummy;
    for (int i = 0; i < kWarmup; i++) {
        lo_send(dest, "/gradient/cancel_all", "s", "bench-node");
        auto dl = Clock::now() + std::chrono::milliseconds(50);
        while (!queue.pop(dummy) && Clock::now() < dl) {}
    }

    std::vector<long long> lat;
    lat.reserve(n);

    for (int i = 0; i < n; i++) {
        auto t_send = Clock::now();
        lo_send(dest, "/gradient/cancel_all", "s", "bench-node");

        auto dl = Clock::now() + std::chrono::milliseconds(100);
        bool got = false;
        while (Clock::now() < dl) {
            if (queue.pop(dummy)) { got = true; break; }
        }
        if (!got) {
            std::fprintf(stderr, "WARN bench_osc_latency: message %d timed out (skipped)\n", i);
            lat.push_back(100'000'000LL);  // sentinel 100 ms
            continue;
        }
        lat.push_back(std::chrono::duration_cast<Ns>(Clock::now() - t_send).count());
    }

    lo_address_free(dest);
    server.stop();
    return lat;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

struct Stats {
    long long p50, p90, p99, max, mean;
};

static Stats computeStats(std::vector<long long> v) {
    std::sort(v.begin(), v.end());
    long long sz = (long long)v.size();
    auto pct = [&](double p) -> long long {
        auto idx = (size_t)(p * sz / 100.0);
        if (idx >= (size_t)sz) idx = (size_t)sz - 1;
        return v[idx];
    };
    long long sum = std::accumulate(v.begin(), v.end(), 0LL);
    return {pct(50), pct(90), pct(99), v.back(), sum / sz};
}

static void printRow(const char* label, const Stats& s,
                     long long p50_lim, long long p99_lim,
                     bool* all_pass) {
    bool p50_ok = s.p50 <= p50_lim;
    bool p99_ok = s.p99 <= p99_lim;
    if (!p50_ok || !p99_ok) *all_pass = false;

    std::printf("  %-36s  mean=%6.3f  p50=%6.3f [%s]  p90=%6.3f  p99=%6.3f [%s]  max=%7.3f  (all ms)\n",
                label,
                s.mean / 1e6,
                s.p50 / 1e6,  p50_ok ? "PASS" : "FAIL",
                s.p90 / 1e6,
                s.p99 / 1e6,  p99_ok ? "PASS" : "FAIL",
                s.max  / 1e6);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    int n = 1000;
    if (argc > 1) n = std::stoi(argv[1]);

    std::printf("bench_osc_latency  N=%d  warmup=%d\n\n", n, kWarmup);
    std::printf("  Mode A: synchronous lo_server  (spec SC-002 target: p50≤1ms, p99≤5ms)\n");
    std::printf("  Mode B: lo_server_thread / OscServer  (production target: p50≤1ms, p99≤5ms — calibrated on dev host)\n\n");

    // ── Mode A ────────────────────────────────────────────────────────────
    auto rawSync = runSyncMode(n);
    Stats sA = computeStats(rawSync);

    // ── Mode B ────────────────────────────────────────────────────────────
    auto rawThread = runThreadMode(n);
    Stats sB = computeStats(rawThread);

    // ── Report ────────────────────────────────────────────────────────────
    bool all_pass = true;
    std::printf("  %-36s  %6s   %6s       %6s   %6s       %6s   %7s\n",
                "Mode", "mean", "p50", "p90", "p99", "", "max");
    std::printf("  %s\n", std::string(110, '-').c_str());
    printRow("A — synchronous lo_server",         sA, kSyncP50Ns,   kSyncP99Ns,   &all_pass);
    printRow("B — lo_server_thread (OscServer)",   sB, kThreadP50Ns, kThreadP99Ns, &all_pass);
    std::printf("\n  %s\n\n", all_pass ? "RESULT: PASS" : "RESULT: FAIL");

    // ── Write results file ────────────────────────────────────────────────
#ifdef BENCH_RESULTS_FILE
    {
        std::ofstream f(BENCH_RESULTS_FILE);
        if (f) {
            f << "bench_osc_latency  N=" << n << "  warmup=" << kWarmup << "\n\n";
            auto row = [&](const char* lbl, const Stats& s,
                           long long p50_lim, long long p99_lim) {
                f << "  " << lbl << "\n"
                  << "    mean=" << s.mean/1e6 << " ms"
                  << "  p50="  << s.p50/1e6  << " ms [" << (s.p50<=p50_lim?"PASS":"FAIL") << "]"
                  << "  p90="  << s.p90/1e6  << " ms"
                  << "  p99="  << s.p99/1e6  << " ms [" << (s.p99<=p99_lim?"PASS":"FAIL") << "]"
                  << "  max="  << s.max/1e6  << " ms\n";
            };
            row("A — synchronous lo_server       (spec SC-002 target p50≤1ms, p99≤5ms)",
                sA, kSyncP50Ns, kSyncP99Ns);
            row("B — lo_server_thread/OscServer  (production target p50≤5ms, p99≤20ms)",
                sB, kThreadP50Ns, kThreadP99Ns);
            f << "\n" << (all_pass ? "RESULT: PASS\n" : "RESULT: FAIL\n");
            std::printf("  Results written to: %s\n\n", BENCH_RESULTS_FILE);
        }
    }
#endif

    return all_pass ? 0 : 1;
}
