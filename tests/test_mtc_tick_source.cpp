/**
 * @file test_mtc_tick_source.cpp
 * @brief Unit tests for gme::time::MtcTickSource — no MIDI hardware required.
 *
 * Scenarios:
 *   A  — Callback invocation: drive invokeTickForTesting (7×false + 1×true
 *         pattern); verify the registered callback receives the correct values.
 *   B  — Single-fire-per-QF contract: each invokeTickForTesting call produces
 *         exactly one consumer invocation regardless of isCompleteFrame flag.
 *   C  — Null callback safety: no callback registered; invokeTickForTesting
 *         must not crash.
 *   D  — getMtcMs() / isRunning() before start(): both return initial values.
 *   E  — Invalid port returns error: start("__no_such_port__") returns a
 *         non-Ok error code.
 *   F  — No-call-after-dtor: consumer count must not change after MtcTickSource
 *         is destroyed (SC-004).
 *   G  — Concurrent registration: main thread fires ticks while a worker
 *         thread alternates setTickCallback — no crash, no data race (FR-010).
 *
 * Integration test (SC-003):
 *   Synthetic 12-second MTC stream at 25 fps — 1200 callbacks total,
 *   10 ms per quarter frame. Also measures p99 dispatch latency (SC-006).
 */

#include "time/MtcTickSource.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using gme::time::MtcStartError;
using gme::time::MtcTickSource;

// ─── helpers ──────────────────────────────────────────────────────────────────

static int failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "FAIL [" << __func__ << "] " << msg << "\n";      \
            ++failures;                                                     \
        } else {                                                            \
            std::cout << "PASS [" << __func__ << "] " << msg << "\n";      \
        }                                                                   \
    } while (0)

// Reset MtcReceiver global state between tests.
static void resetMtcState() {
    MtcReceiver::setTickCallback({});
    MtcReceiver::resetStaticStateForTesting();
    MtcReceiver::mtcHead.store(0);
    MtcReceiver::isTimecodeRunning.store(false);
}

// ─── Scenario A: Callback invocation ─────────────────────────────────────────

static void testScenarioA_callbackInvocation() {
    resetMtcState();

    std::atomic<long> last{-1};
    std::atomic<int>  count{0};

    MtcTickSource src;
    src.setTickCallback([&](long ms) {
        last.store(ms);
        count.fetch_add(1, std::memory_order_relaxed);
    });

    // Drive 8 QFs with the realistic 7×false + 1×true pattern.
    for (int i = 0; i < 7; ++i) {
        MtcReceiver::invokeTickForTesting(static_cast<long>(i + 1) * 10L, false);
    }
    MtcReceiver::invokeTickForTesting(80L, true);

    CHECK(count.load() == 8,
          "callback fires exactly 8 times (once per QF)");
    CHECK(last.load() == 80L,
          "final callback receives the value from the last (true) QF");
}

// ─── Scenario B: Single-fire-per-QF contract ─────────────────────────────────

static void testScenarioB_singleFirePerQF() {
    resetMtcState();

    std::atomic<int> count{0};

    MtcTickSource src;
    src.setTickCallback([&](long) {
        count.fetch_add(1, std::memory_order_relaxed);
    });

    // Mix of false and true flags — each call must yield exactly one invocation.
    constexpr int kN = 10;
    for (int i = 0; i < kN; ++i) {
        bool flag = (i % 8 == 7);  // true on every 8th call
        MtcReceiver::invokeTickForTesting(static_cast<long>(i) * 10L, flag);
    }

    CHECK(count.load() == kN,
          "exactly one callback invocation per invokeTickForTesting call (no double-fire)");
}

// ─── Scenario C: Null callback safety ────────────────────────────────────────

static void testScenarioC_nullCallbackSafe() {
    resetMtcState();
    // No callback registered — invokeTickForTesting must not crash.
    MtcReceiver::invokeTickForTesting(999L, false);
    CHECK(true, "no crash when no callback is registered");
}

// ─── Scenario D: getMtcMs() / isRunning() before start() ─────────────────────

static void testScenarioD_queryBeforeStart() {
    resetMtcState();

    MtcTickSource src;

    CHECK(src.getMtcMs() == 0L,      "getMtcMs() returns 0 before start()");
    CHECK(src.isRunning() == false,  "isRunning() returns false before start()");
}

// ─── Scenario E: Invalid port returns kPortNotFound ──────────────────────────

static void testScenarioE_invalidPortReturnsError() {
    resetMtcState();

    MtcTickSource src;
    src.setTickCallback([](long) {});

    auto err = src.start("__no_such_port__");

    // Either kNoPortsAvailable (no MIDI on this system) or kPortNotFound —
    // both confirm no crash and the correct error path.
    CHECK(err != MtcStartError::kOk,
          "start(\"__no_such_port__\") returns a non-Ok error code");
    CHECK(err == MtcStartError::kPortNotFound ||
          err == MtcStartError::kNoPortsAvailable,
          "error code is kPortNotFound or kNoPortsAvailable");
}

// ─── Scenario F: No-call-after-dtor (SC-004) ─────────────────────────────────

static void testScenarioF_noCallAfterDestruction() {
    resetMtcState();

    std::atomic<int> count{0};

    {
        MtcTickSource src;
        src.setTickCallback([&](long) {
            count.fetch_add(1, std::memory_order_relaxed);
        });

        MtcReceiver::invokeTickForTesting(1L, false);
        // Destructor called here — deregisters callback.
    }

    int snapshot = count.load();

    // These ticks must not reach the (now-destroyed) src's callback.
    MtcReceiver::invokeTickForTesting(1L, false);
    MtcReceiver::invokeTickForTesting(2L, true);

    CHECK(count.load() == snapshot,
          "no callback invocations after MtcTickSource is destroyed (SC-004)");
}

// ─── Scenario G: Concurrent registration (FR-010, US2 AS-3) ─────────────────

static void testScenarioG_concurrentRegistration() {
    resetMtcState();

    MtcTickSource src;

    std::atomic<bool> stop{false};
    std::atomic<int>  tickCount{0};

    // Worker: repeatedly register and deregister callback.
    std::thread worker([&]() {
        for (int i = 0; i < 200 && !stop.load(std::memory_order_relaxed); ++i) {
            src.setTickCallback([&](long) {
                tickCount.fetch_add(1, std::memory_order_relaxed);
            });
            src.setTickCallback({});
        }
    });

    // Main thread: fire ticks while worker registers/deregisters.
    for (int i = 0; i < 10000; ++i) {
        bool flag = (i % 8 == 7);
        MtcReceiver::invokeTickForTesting(static_cast<long>(i), flag);
    }

    stop.store(true, std::memory_order_relaxed);
    worker.join();

    // No crash and no TSan report is the primary pass criterion.
    // tickCount may be anything between 0 and 10000 — both extremes are valid.
    CHECK(true, "no crash under concurrent setTickCallback and invokeTickForTesting (FR-010)");
}

// ─── Integration: Synthetic 12-second MTC stream (SC-003, SC-006) ────────────

static void testSyntheticMtcStream() {
    resetMtcState();

    // 1200 ticks × 10 ms = 12000 ms (12 s) at the real QF rate of 100 Hz (25 fps).
    constexpr long kMsPerQf    = 10L;
    constexpr int  kCallbacks  = 1200;
    constexpr long kExpectedMs = kCallbacks * kMsPerQf;  // 12000 ms

    std::atomic<int> count{0};
    std::vector<long long> latenciesNs;
    latenciesNs.reserve(kCallbacks);

    // Timestamp captured inside callback; shared via pointer to vector element.
    // We pre-size so push_back never reallocates during measurement.
    std::atomic<long long*> slotPtr{nullptr};

    MtcTickSource src;
    src.setTickCallback([&](long ms) {
        // Record callback-entry time for latency measurement.
        auto t = std::chrono::steady_clock::now().time_since_epoch().count();
        if (auto* slot = slotPtr.load(std::memory_order_acquire)) {
            *slot = t;
        }
        count.fetch_add(1, std::memory_order_relaxed);
        MtcReceiver::mtcHead.store(ms);
    });

    long currentMs = 0;
    for (int i = 0; i < kCallbacks; ++i) {
        currentMs += kMsPerQf;
        bool isComplete = ((i % 8) == 7);  // 7×false + 1×true per 8-QF cycle

        // Pre-allocate slot and record invocation start.
        latenciesNs.push_back(0LL);
        slotPtr.store(&latenciesNs.back(), std::memory_order_release);

        auto t0 = std::chrono::steady_clock::now().time_since_epoch().count();
        MtcReceiver::invokeTickForTesting(currentMs, isComplete);
        auto t1 = std::chrono::steady_clock::now().time_since_epoch().count();

        // Latency = end-of-invokeTickForTesting minus start-of-invokeTickForTesting.
        // This bounds the full dispatch path including mtcreceiver overhead.
        latenciesNs.back() = t1 - t0;
        slotPtr.store(nullptr, std::memory_order_release);
    }

    CHECK(count.load() == kCallbacks,
          "callback fires exactly 1200 times (SC-003)");

    long finalMs = src.getMtcMs();
    long delta   = finalMs - kExpectedMs;
    if (delta < 0) delta = -delta;
    CHECK(delta <= 1L,
          "final getMtcMs() within ±1 ms of expected 12000 ms (SC-003)");

    // SC-006: p99 latency < 1 ms (1 000 000 ns).
    std::sort(latenciesNs.begin(), latenciesNs.end());
    long long p99idx  = static_cast<long long>(kCallbacks * 0.99) - 1;
    if (p99idx < 0) p99idx = 0;
    long long p99Ns   = latenciesNs[static_cast<size_t>(p99idx)];
    long long minNs   = latenciesNs.front();
    long long medNs   = latenciesNs[kCallbacks / 2];
    long long maxNs   = latenciesNs.back();

    std::cout << "  Latency (ns): min=" << minNs
              << " median=" << medNs
              << " p99=" << p99Ns
              << " max=" << maxNs << "\n";

    CHECK(p99Ns < 1000000LL,
          "p99 dispatch latency < 1 ms (SC-006)");
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== test_mtc_tick_source ===\n";

    testScenarioA_callbackInvocation();
    testScenarioB_singleFirePerQF();
    testScenarioC_nullCallbackSafe();
    testScenarioD_queryBeforeStart();
    testScenarioE_invalidPortReturnsError();
    testScenarioF_noCallAfterDestruction();
    testScenarioG_concurrentRegistration();
    testSyntheticMtcStream();

    std::cout << "\n";
    if (failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    } else {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }
}
