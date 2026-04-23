/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file test_motion_registry_bench.cpp
 * @brief SC-007 loopback OSC benchmark: 50 concurrent fades at 200 Hz.
 *
 * Scenario 1 — FadeMotion (real curve + real OSC send to loopback):
 *   50 fades × 400 ticks at 5 ms intervals. p99 must be ≤ 2 ms.
 *
 * Scenario 2 — TestMotion no-op (isolates registry overhead from FadeMotion work):
 *   50 TestMotion instances whose evalAndSend is a no-op. The delta between
 *   Scenario 1 and Scenario 2 is the virtual-dispatch + FadeMotion work cost.
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "motion/FadeMotion.h"
#include "motion/IMotion.h"
#include "motion/MotionRegistry.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"
#include "signal/StatusEmitRequest.h"
#include "time/MtcTickSource.h"

// ---------------------------------------------------------------------------
// Scenario 1: FadeMotion via apply()
// ---------------------------------------------------------------------------

static gme::signal::FadeCommand makeCmd(int idx) {
    gme::signal::FadeCommand cmd;
    cmd.type         = gme::signal::FadeCommand::Type::START_FADE;
    cmd.fade_id      = "bench" + std::to_string(idx);
    cmd.curve_type   = "sigmoid";
    cmd.start_value  = 0.0f;
    cmd.end_value    = 1.0f;
    cmd.duration_ms  = 200000.0f;
    cmd.start_mtc_ms = 0;
    cmd.osc_host     = "127.0.0.1";
    cmd.osc_port     = 9996;
    cmd.osc_path     = "/bench/" + std::to_string(idx);
    return cmd;
}

static bool runFadeMotionBench() {
    gme::time::MtcTickSource tickSrc;
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> sq;

    gme::motion::MotionRegistry reg(tickSrc, sq,
        [](gme::signal::StatusKind, const std::string&, const std::string&) {});

    for (int i = 0; i < 50; ++i) {
        auto cmd = makeCmd(i);
        reg.apply(cmd);
    }
    std::fprintf(stdout, "[FadeMotion bench] Registered %zu motions\n", reg.size());

    // Warm-up
    for (int i = 0; i < 10; ++i) reg.tick(i * 5);

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
    long long p99 = durations_us[static_cast<int>(kTicks * 0.99)];

    std::fprintf(stdout,
        "[FadeMotion bench] Tick duration (50 fades, %d ticks):\n"
        "  min    = %lld µs\n"
        "  median = %lld µs\n"
        "  p99    = %lld µs  (budget: 2000 µs)\n"
        "  max    = %lld µs\n",
        kTicks,
        durations_us.front(),
        durations_us[kTicks / 2],
        p99,
        durations_us.back());

    if (p99 > 2000) {
        std::fprintf(stderr,
            "FAIL SC-007 [FadeMotion]: p99 tick duration %lld µs exceeds 2 ms budget\n",
            p99);
        return false;
    }
    std::fprintf(stdout, "PASS SC-007 [FadeMotion]: p99 = %lld µs ≤ 2000 µs\n", p99);
    return true;
}

// ---------------------------------------------------------------------------
// Scenario 2: TestMotion no-op (registry overhead isolation)
// ---------------------------------------------------------------------------

struct NoOpMotion final : gme::motion::IMotion {
    NoOpMotion(int idx) {
        motion_id    = "noop" + std::to_string(idx);
        osc_key      = "127.0.0.1:9996:/noop/" + std::to_string(idx);
        duration_ms  = 200000.0f;
        start_mtc_ms = 0;
    }
    gme::motion::EvalResult evalAndSend(long) override { return {}; }
    void sendSnapToEnd() override {}
    void inheritFrom(const gme::motion::IMotion&) override {}
};

static bool runNoOpBench() {
    gme::time::MtcTickSource tickSrc;
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> sq;

    gme::motion::MotionRegistry reg(tickSrc, sq,
        [](gme::signal::StatusKind, const std::string&, const std::string&) {});

    for (int i = 0; i < 50; ++i)
        reg.addMotion(std::make_unique<NoOpMotion>(i));
    std::fprintf(stdout, "[NoOp bench] Registered %zu motions\n", reg.size());

    for (int i = 0; i < 10; ++i) reg.tick(i * 5);

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
    long long p99 = durations_us[static_cast<int>(kTicks * 0.99)];

    std::fprintf(stdout,
        "[NoOp bench] Tick duration (50 no-op motions, %d ticks):\n"
        "  min    = %lld µs\n"
        "  median = %lld µs\n"
        "  p99    = %lld µs\n"
        "  max    = %lld µs\n",
        kTicks,
        durations_us.front(),
        durations_us[kTicks / 2],
        p99,
        durations_us.back());

    std::fprintf(stdout, "INFO [NoOp bench]: registry overhead isolated (no OSC, no curve eval)\n");
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    bool ok1 = runFadeMotionBench();
    bool ok2 = runNoOpBench();
    return (ok1 && ok2) ? 0 : 1;
}
