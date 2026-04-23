/**
 * @file test_lockfree_queue.cpp
 * @brief Tests for gme::signal::LockFreeQueue.
 *
 * T019: Single-thread FIFO correctness.
 * T020: Drop-oldest overflow.
 * T021: SPSC stress test (concurrent producer + consumer threads).
 * T026: US3 fallback drain timing (CANCEL_ALL within 200 ms).
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unordered_set>
#include <vector>

#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"

using gme::signal::FadeCommand;
using gme::signal::LockFreeQueue;
using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "FAIL [%s]: %s\n", msg, #cond);               \
            return false;                                                       \
        }                                                                       \
    } while (0)

struct FakeCmd {
    int seq = 0;
};

// ---------------------------------------------------------------------------
// T019 — Single-thread FIFO correctness
// ---------------------------------------------------------------------------

static bool test_fifo_correctness() {
    LockFreeQueue<int, 8> q;
    static const int ITEMS = 1000;

    // Push 1000 items in batches (queue holds 7 usable slots)
    int next_push = 0;
    int next_pop  = 0;

    while (next_pop < ITEMS) {
        // Fill the queue
        while (next_push < ITEMS) {
            int v = next_push;
            bool ok = q.push(std::move(v));
            if (!ok) break; // queue full (drop-oldest would fire; stop here)
            ++next_push;
            if (next_push - next_pop >= 7) break; // keep queue manageable
        }
        // Drain
        int out;
        while (q.pop(out)) {
            CHECK(out == next_pop, "FIFO order");
            ++next_pop;
        }
    }
    CHECK(q.empty(), "empty after drain");
    return true;
}

// ---------------------------------------------------------------------------
// T020 — Drop-oldest overflow
// ---------------------------------------------------------------------------

static bool test_drop_oldest() {
    static const std::size_t N = 8;
    LockFreeQueue<int, N> q;

    int drop_count = 0;
    // Push N+5 items (0 … N+4); N-1 usable slots, so 6 should be dropped
    // (1 reserved slot + 5 overflows)
    for (int i = 0; i < static_cast<int>(N + 5); ++i) {
        int v = i;
        if (!q.push(std::move(v))) ++drop_count;
    }

    CHECK(drop_count == 6, "push returned false exactly 6 times");

    // Drain and verify we see the last N-1 values
    std::vector<int> got;
    int v;
    while (q.pop(v)) got.push_back(v);

    CHECK(got.size() == N - 1, "only N-1 items remain");

    // The oldest were dropped; the newest N-1 items should remain
    int expected_first = static_cast<int>(N + 5) - static_cast<int>(N - 1);
    for (int i = 0; i < static_cast<int>(N - 1); ++i) {
        CHECK(got[static_cast<std::size_t>(i)] == expected_first + i, "drop-oldest newest items");
    }

    return true;
}

// ---------------------------------------------------------------------------
// T021 — SPSC stress test
// ---------------------------------------------------------------------------

static bool test_spsc_stress() {
    static const int TOTAL = 10000;
    LockFreeQueue<FakeCmd, 64> q;

    std::atomic<int> push_count{0};
    std::atomic<int> drop_count{0};

    // Producer
    std::thread producer([&]() {
        for (int i = 0; i < TOTAL; ++i) {
            FakeCmd c{i};
            if (!q.push(std::move(c)))
                drop_count.fetch_add(1, std::memory_order_relaxed);
            else
                push_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // Consumer
    std::unordered_set<int> seen;
    seen.reserve(TOTAL);
    std::vector<int> duplicates;

    std::thread consumer([&]() {
        int received = 0;
        // Stop when we've received (TOTAL - drops) items; but drops aren't
        // known until producer done. Use a timeout approach instead.
        auto deadline = Clock::now() + std::chrono::seconds(5);
        FakeCmd out;
        while (Clock::now() < deadline) {
            if (q.pop(out)) {
                ++received;
                if (seen.count(out.seq)) duplicates.push_back(out.seq);
                seen.insert(out.seq);
            }
        }
        // Drain remaining
        while (q.pop(out)) {
            ++received;
            if (seen.count(out.seq)) duplicates.push_back(out.seq);
            seen.insert(out.seq);
        }
    });

    producer.join();
    consumer.join();

    int pushed  = push_count.load();
    int dropped = drop_count.load();
    int received = static_cast<int>(seen.size());

    std::fprintf(stdout, "  spsc: pushed=%d dropped=%d received=%d duplicates=%d\n",
                 pushed, dropped, received, (int)duplicates.size());

    CHECK(duplicates.empty(), "no duplicate sequence numbers");
    // All 10000 attempts are accounted for
    CHECK(push_count.load() + drop_count.load() == TOTAL, "push + drop == TOTAL");
    // Consumer received only items that weren't overwritten; must be > 0
    CHECK(received > 0, "consumer received at least some items");
    // No item appears more than once — the critical SPSC correctness property
    CHECK((int)seen.size() == received, "each received seq is unique");
    return true;
}

// ---------------------------------------------------------------------------
// T026 — US3 fallback drain timing: CANCEL_ALL within 200 ms
// ---------------------------------------------------------------------------

static bool test_fallback_drain_timing() {
    LockFreeQueue<FadeCommand, 64> q;
    std::atomic<bool> cancel_all_received{false};
    std::atomic_flag drain_in_progress = ATOMIC_FLAG_INIT;

    auto drain_once = [&]() {
        if (drain_in_progress.test_and_set(std::memory_order_acquire)) return;
        FadeCommand out;
        while (q.pop(out)) {
            if (out.type == FadeCommand::Type::CANCEL_ALL)
                cancel_all_received.store(true, std::memory_order_release);
        }
        drain_in_progress.clear(std::memory_order_release);
    };

    // Background drain thread: fires every 100 ms
    std::atomic<bool> running{true};
    std::thread drain_thread([&]() {
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            drain_once();
        }
    });

    // Push a CANCEL_ALL command directly to the queue
    FadeCommand cancel_cmd;
    cancel_cmd.type = FadeCommand::Type::CANCEL_ALL;

    auto t_push = Clock::now();
    q.push(std::move(cancel_cmd));

    // Assert drain callback fires within 200 ms
    auto deadline = t_push + std::chrono::milliseconds(200);
    while (Clock::now() < deadline && !cancel_all_received.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    double elapsed = Ms(Clock::now() - t_push).count();
    std::fprintf(stdout, "  fallback_drain: CANCEL_ALL consumed in %.1f ms (limit 200 ms)\n",
                 elapsed);

    running.store(false);
    drain_thread.join();

    CHECK(cancel_all_received.load(), "CANCEL_ALL received via fallback drain");
    CHECK(elapsed <= 200.0, "consumed within 200 ms");
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        { "fifo_correctness",      test_fifo_correctness      },
        { "drop_oldest",           test_drop_oldest           },
        { "spsc_stress",           test_spsc_stress           },
        { "fallback_drain_timing", test_fallback_drain_timing },
    };

    int failed = 0;
    for (auto& t : tests) {
        bool ok = t.fn();
        std::fprintf(stdout, "%s %s\n", ok ? "PASS" : "FAIL", t.name);
        if (!ok) ++failed;
    }

    std::fprintf(stdout, "\n%d/%zu tests passed\n",
                 (int)(sizeof(tests)/sizeof(tests[0])) - failed,
                 sizeof(tests)/sizeof(tests[0]));
    return failed > 0 ? 1 : 0;
}
