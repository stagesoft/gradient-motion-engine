/**
 * @file test_fade_registry_bench.cpp
 * @brief SC-007 loopback OSC benchmark: 50 concurrent fades at 200 Hz.
 *
 * Measures wall-clock tick duration with the real `gme::osc::sendFloat`
 * sending to 127.0.0.1 (loopback). Passes if the p99 tick duration is
 * ≤ 2 ms (leaving ≥ 3 ms headroom before the next 5 ms tick at 200 Hz).
 *
 * The benchmark runs 400 ticks (2 s simulated) then reports min/median/p99/max.
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/FadeRegistry.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"
#include "signal/StatusEmitRequest.h"
#include "time/MtcTickSource.h"

static gme::signal::FadeCommand makeCmd(int idx) {
    gme::signal::FadeCommand cmd;
    cmd.type         = gme::signal::FadeCommand::Type::START_FADE;
    cmd.fade_id      = "bench" + std::to_string(idx);
    cmd.curve_type   = "sigmoid";
    cmd.start_value  = 0.0f;
    cmd.end_value    = 1.0f;
    cmd.duration_ms  = 200000.0f;  // very long — won't complete during bench
    cmd.start_mtc_ms = 0;
    cmd.osc_host     = "127.0.0.1";
    cmd.osc_port     = 9996;       // nothing listening — fire-and-forget UDP
    cmd.osc_path     = "/bench/" + std::to_string(idx);
    return cmd;
}

int main() {
    gme::time::MtcTickSource tickSrc;
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> sq;

    gme::engine::FadeRegistry reg(tickSrc, sq,
        [](gme::signal::StatusKind, const std::string&, const std::string&) {});

    // Register 50 concurrent fades
    for (int i = 0; i < 50; ++i) {
        auto cmd = makeCmd(i);
        reg.apply(cmd);
    }
    std::fprintf(stdout, "Registered %zu fades\n", reg.size());

    // Warm-up (10 ticks)
    for (int i = 0; i < 10; ++i) reg.tick(i * 5);

    // Benchmark: 400 ticks at 5 ms intervals (simulating 200 Hz)
    constexpr int kTicks = 400;
    std::vector<long long> durations_us;
    durations_us.reserve(kTicks);

    for (int i = 0; i < kTicks; ++i) {
        long mtc_ms = 10 + i * 5;

        auto t0 = std::chrono::steady_clock::now();
        reg.tick(mtc_ms);
        auto t1 = std::chrono::steady_clock::now();

        durations_us.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    }

    std::sort(durations_us.begin(), durations_us.end());

    long long min_us    = durations_us.front();
    long long median_us = durations_us[kTicks / 2];
    long long p99_us    = durations_us[static_cast<int>(kTicks * 0.99)];
    long long max_us    = durations_us.back();

    std::fprintf(stdout,
        "Tick duration (50 fades, %d ticks):\n"
        "  min    = %lld µs\n"
        "  median = %lld µs\n"
        "  p99    = %lld µs  (budget: 2000 µs)\n"
        "  max    = %lld µs\n",
        kTicks, min_us, median_us, p99_us, max_us);

    if (p99_us > 2000) {
        std::fprintf(stderr,
            "FAIL SC-007: p99 tick duration %lld µs exceeds 2 ms budget\n",
            p99_us);
        return 1;
    }

    std::fprintf(stdout, "PASS SC-007: p99 = %lld µs ≤ 2000 µs\n", p99_us);
    return 0;
}
