/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file test_motion_registry.cpp
 * @brief Lifecycle-only unit tests for MotionRegistry, using a TestMotion
 *        double that scripts EvalResult outcomes without any OSC coupling.
 *
 * Also contains the LSP contract test that constructs a real FadeMotion and
 * exercises the registry via the IMotion interface.
 */

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gradient/CurveFactory.h"
#include "motion/FadeMotion.h"
#include "motion/IMotion.h"
#include "motion/MotionRegistry.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"
#include "signal/StatusEmitRequest.h"
#include "time/MtcTickSource.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { \
        std::fprintf(stderr, "FAIL [%s]: %s (line %d)\n", msg, #cond, __LINE__); \
        return false; \
    } } while(0)

// ---------------------------------------------------------------------------
// TestMotion — scripted IMotion double
// ---------------------------------------------------------------------------

/**
 * Concrete IMotion whose evalAndSend returns EvalResults from a script
 * vector in order. Transport send counter and inheritFrom tracking for
 * verification.
 */
struct TestMotion final : gme::motion::IMotion {
    std::vector<gme::motion::EvalResult> script;
    int eval_calls        = 0;
    int snap_calls        = 0;
    const IMotion* inherit_arg = nullptr;

    TestMotion(std::string id, std::string key) {
        motion_id = std::move(id);
        osc_key   = std::move(key);
        duration_ms  = 10000.0f;
        start_mtc_ms = 0;
    }

    gme::motion::EvalResult evalAndSend(long) override {
        if (eval_calls < (int)script.size())
            return script[eval_calls++];
        ++eval_calls;
        return {};  // default: not completed, not failed
    }

    void sendSnapToEnd() override { ++snap_calls; }

    void inheritFrom(const IMotion& prior) override {
        inherit_arg = &prior;
    }
};

static std::unique_ptr<TestMotion> makeMotion(const std::string& id,
                                               const std::string& key = "") {
    std::string k = key.empty() ? "127.0.0.1:9999:" + id : key;
    return std::make_unique<TestMotion>(id, k);
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

struct Ctx {
    gme::time::MtcTickSource                                       tickSrc;
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> sq;
    std::vector<gme::signal::StatusEmitRequest>                    emitted;

    Ctx() {}

    void drainQueue() {
        gme::signal::StatusEmitRequest r;
        while (sq.pop(r)) emitted.push_back(r);
    }

    std::unique_ptr<gme::motion::MotionRegistry> makeReg() {
        return std::make_unique<gme::motion::MotionRegistry>(
            tickSrc, sq,
            [this](gme::signal::StatusKind k,
                   const std::string& id,
                   const std::string& reason) {
                gme::signal::StatusEmitRequest r;
                r.kind    = k;
                r.fade_id = id;
                r.reason  = reason;
                emitted.push_back(r);
            });
    }

    bool hasStatus(gme::signal::StatusKind k,
                   const std::string& id,
                   const std::string& reason = "") const {
        for (auto& e : emitted) {
            if (e.kind == k && e.fade_id == id &&
                (reason.empty() || e.reason == reason))
                return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/** addMotion inserts into both motions_ and osc_index_ (observable via size). */
static bool test_add_motion_inserts() {
    Ctx ctx;
    auto reg = ctx.makeReg();

    reg->addMotion(makeMotion("m1", "host:1:/a"));
    ASSERT_TRUE(reg->size() == 1, "add_inserts: size == 1");

    reg->addMotion(makeMotion("m2", "host:1:/b"));
    ASSERT_TRUE(reg->size() == 2, "add_inserts: size == 2");
    return true;
}

/** Supersede: same osc_key, different motion_id emits MotionError:superseded
 *  for old id, calls inheritFrom on new motion, erases old. */
static bool test_add_supersede() {
    Ctx ctx;
    auto reg = ctx.makeReg();

    const std::string key = "host:1:/vol";
    auto* m2_raw = new TestMotion("m2", key);   // keep raw ptr before move
    reg->addMotion(makeMotion("m1", key));
    ASSERT_TRUE(reg->size() == 1, "supersede: m1 inserted");

    std::unique_ptr<TestMotion> m2(m2_raw);
    reg->addMotion(std::move(m2));
    ASSERT_TRUE(reg->size() == 1, "supersede: only m2 remains");

    ASSERT_TRUE(ctx.hasStatus(gme::signal::StatusKind::MotionError, "m1", "superseded"),
                "supersede: MotionError:superseded for m1");
    ASSERT_TRUE(m2_raw->inherit_arg != nullptr, "supersede: inheritFrom called");
    return true;
}

/** Duplicate motion_id (different key): incoming rejected, existing untouched. */
static bool test_add_duplicate_id_rejects() {
    Ctx ctx;
    auto reg = ctx.makeReg();

    reg->addMotion(makeMotion("m1", "host:1:/a"));
    ASSERT_TRUE(reg->size() == 1, "dup_id: m1 on /a inserted");

    int snap_count_before = 0;
    auto* incoming = new TestMotion("m1", "host:1:/b");
    reg->addMotion(std::unique_ptr<TestMotion>(incoming));

    ASSERT_TRUE(reg->size() == 1, "dup_id: size still 1 (existing untouched)");
    ASSERT_TRUE(ctx.hasStatus(gme::signal::StatusKind::MotionError,
                              "m1", "duplicate_motion_id"),
                "dup_id: MotionError:duplicate_motion_id emitted for incoming");
    (void)snap_count_before;
    return true;
}

/** Duplicate motion_id + same osc_key: id check runs BEFORE supersede check. */
static bool test_add_duplicate_id_same_path_rejects() {
    Ctx ctx;
    auto reg = ctx.makeReg();

    const std::string key = "host:1:/a";
    reg->addMotion(makeMotion("m1", key));
    ctx.emitted.clear();

    reg->addMotion(makeMotion("m1", key));  // same id AND same key

    ASSERT_TRUE(reg->size() == 1, "dup_id_same: size still 1");
    ASSERT_TRUE(ctx.hasStatus(gme::signal::StatusKind::MotionError,
                              "m1", "duplicate_motion_id"),
                "dup_id_same: duplicate_motion_id, NOT superseded");
    ASSERT_TRUE(!ctx.hasStatus(gme::signal::StatusKind::MotionError, "m1", "superseded"),
                "dup_id_same: superseded NOT emitted");
    return true;
}

/** cancelMotion(snap_to_end=true) calls sendSnapToEnd exactly once. */
static bool test_cancel_snap_calls_snap() {
    Ctx ctx;
    auto reg = ctx.makeReg();

    auto* m = new TestMotion("m1", "host:1:/a");
    reg->addMotion(std::unique_ptr<TestMotion>(m));
    ASSERT_TRUE(reg->size() == 1, "cancel_snap: inserted");

    reg->cancelMotion("m1", /*snap_to_end=*/true);
    ASSERT_TRUE(m->snap_calls == 1, "cancel_snap: sendSnapToEnd called once");
    ASSERT_TRUE(reg->size() == 0, "cancel_snap: removed");
    return true;
}

/** cancelMotion(snap_to_end=false) does NOT call sendSnapToEnd. */
static bool test_cancel_hold_no_snap() {
    Ctx ctx;
    auto reg = ctx.makeReg();

    auto* m = new TestMotion("m1", "host:1:/a");
    reg->addMotion(std::unique_ptr<TestMotion>(m));

    reg->cancelMotion("m1", /*snap_to_end=*/false);
    ASSERT_TRUE(m->snap_calls == 0, "cancel_hold: sendSnapToEnd NOT called");
    ASSERT_TRUE(reg->size() == 0, "cancel_hold: removed");
    return true;
}

/** cancelAll() removes all motions and never calls sendSnapToEnd. */
static bool test_cancel_all_no_snap() {
    Ctx ctx;
    auto reg = ctx.makeReg();

    TestMotion* ptrs[3];
    for (int i = 0; i < 3; ++i) {
        ptrs[i] = new TestMotion("m" + std::to_string(i),
                                  "host:1:/" + std::to_string(i));
        reg->addMotion(std::unique_ptr<TestMotion>(ptrs[i]));
    }
    ASSERT_TRUE(reg->size() == 3, "cancel_all: inserted 3");

    auto t0 = std::chrono::steady_clock::now();
    reg->cancelAll();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    ASSERT_TRUE(reg->size() == 0, "cancel_all: all removed");
    ASSERT_TRUE(ms < 5, "cancel_all: completes < 5 ms (SC-004)");
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(ptrs[i]->snap_calls == 0, "cancel_all: no sendSnapToEnd");
    return true;
}

/** tick() removes motion that returned completed=true and emits MotionComplete. */
static bool test_tick_removes_completed() {
    Ctx ctx;
    auto reg = ctx.makeReg();
    reg->setTickThreadContext(true);

    auto* m = new TestMotion("m1", "host:1:/a");
    m->script = { {true, false, nullptr} };  // completed on first tick
    reg->addMotion(std::unique_ptr<TestMotion>(m));

    reg->tick(0);
    ASSERT_TRUE(reg->size() == 0, "tick_complete: removed after completion");

    ctx.drainQueue();
    ASSERT_TRUE(ctx.hasStatus(gme::signal::StatusKind::MotionComplete, "m1"),
                "tick_complete: MotionComplete emitted");
    return true;
}

/** tick() increments consecutive_osc_failures on failed, removes at threshold. */
static bool test_tick_osc_failure_threshold() {
    Ctx ctx;
    auto reg = ctx.makeReg();
    reg->setTickThreadContext(true);

    auto* m = new TestMotion("m1", "host:1:/a");
    // All ticks fail
    for (int i = 0; i < gme::motion::MotionRegistry::kOscFailureThreshold + 2; ++i)
        m->script.push_back({false, true, "osc_send_failed"});
    reg->addMotion(std::unique_ptr<TestMotion>(m));

    // kOscFailureThreshold ticks
    for (int i = 0; i < gme::motion::MotionRegistry::kOscFailureThreshold; ++i)
        reg->tick(i * 5);

    ASSERT_TRUE(reg->size() == 0, "tick_fail_thresh: removed at threshold");

    ctx.drainQueue();
    ASSERT_TRUE(ctx.hasStatus(gme::signal::StatusKind::MotionError,
                              "m1", "osc_send_failed"),
                "tick_fail_thresh: MotionError:osc_send_failed emitted");
    return true;
}

/** tick() resets consecutive_osc_failures to 0 on success after prior failures. */
static bool test_tick_failure_reset_on_success() {
    Ctx ctx;
    auto reg = ctx.makeReg();
    reg->setTickThreadContext(true);

    auto* m = new TestMotion("m1", "host:1:/a");
    // 3 failures then 1 success — below threshold, should survive
    for (int i = 0; i < 3; ++i) m->script.push_back({false, true, "osc_send_failed"});
    m->script.push_back({false, false, nullptr});  // success
    reg->addMotion(std::unique_ptr<TestMotion>(m));

    for (int i = 0; i < 4; ++i) reg->tick(i * 5);
    ASSERT_TRUE(reg->size() == 1, "tick_reset: alive after transient failures");
    ASSERT_TRUE(m->consecutive_osc_failures == 0, "tick_reset: counter reset to 0");

    ctx.drainQueue();
    ASSERT_TRUE(!ctx.hasStatus(gme::signal::StatusKind::MotionError, "m1", "osc_send_failed"),
                "tick_reset: no terminal error emitted");
    return true;
}

/** 70 completions with 64-slot queue → ≤ 64 statuses, no tick-thread block. */
static bool test_status_queue_overflow() {
    Ctx ctx;
    auto reg = ctx.makeReg();
    reg->setTickThreadContext(true);

    for (int i = 0; i < 70; ++i) {
        auto* m = new TestMotion("m" + std::to_string(i),
                                  "host:1:/" + std::to_string(i));
        m->script = { {true, false, nullptr} };
        reg->addMotion(std::unique_ptr<TestMotion>(m));
        reg->tick(0);
    }
    ctx.drainQueue();
    ASSERT_TRUE(ctx.emitted.size() <= 64, "queue_overflow: ≤ 64 statuses");
    ASSERT_TRUE(ctx.emitted.size() > 0,   "queue_overflow: some statuses");
    return true;
}

/** Drain-thread-context supersede emits via statusDirect_ (not queue). */
static bool test_status_routing_drain_context() {
    Ctx ctx;
    auto reg = ctx.makeReg();
    // tickThreadContext_ = false (drain context, default)

    const std::string key = "host:1:/x";
    reg->addMotion(makeMotion("m1", key));
    reg->addMotion(makeMotion("m2", key));  // supersedes m1

    // Drain context → emitted directly (already in ctx.emitted via direct cb)
    ASSERT_TRUE(ctx.hasStatus(gme::signal::StatusKind::MotionError, "m1", "superseded"),
                "drain_route: supersede status via statusDirect_");
    // Queue should be empty (not pushed to statusQueue_)
    gme::signal::StatusEmitRequest r;
    ASSERT_TRUE(!ctx.sq.pop(r), "drain_route: statusQueue_ is empty");
    return true;
}

/** Tick-thread-context supersede pushes into statusQueue_ (not direct). */
static bool test_status_routing_tick_context() {
    Ctx ctx;
    auto reg = ctx.makeReg();
    reg->setTickThreadContext(true);

    const std::string key = "host:1:/y";
    reg->addMotion(makeMotion("m1", key));
    ctx.emitted.clear();  // clear drain-context supersede from m1's own add

    reg->addMotion(makeMotion("m2", key));

    // Tick context → should be in queue, not in emitted
    ASSERT_TRUE(ctx.emitted.empty(), "tick_route: not in direct emitted");
    ctx.drainQueue();
    ASSERT_TRUE(ctx.hasStatus(gme::signal::StatusKind::MotionError, "m1", "superseded"),
                "tick_route: supersede status via queue");
    return true;
}

// ---------------------------------------------------------------------------
// LSP contract test — real FadeMotion through MotionRegistry interface
// ---------------------------------------------------------------------------

static bool test_lsp_fade_motion_through_registry() {
    gme::time::MtcTickSource tickSrc;
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> sq;
    std::vector<gme::signal::StatusEmitRequest> emitted;

    gme::motion::MotionRegistry reg(
        tickSrc, sq,
        [&](gme::signal::StatusKind k, const std::string& id, const std::string& r) {
            gme::signal::StatusEmitRequest req;
            req.kind = k; req.fade_id = id; req.reason = r;
            emitted.push_back(req);
        },
        [](lo_address, const char*, float) -> int { return 0; });

    // Build a FadeMotion directly and hand it to the registry
    lo_address addr = lo_address_new("127.0.0.1", "9995");
    ASSERT_TRUE(addr != nullptr, "lsp: lo_address_new succeeded");

    auto curve_opt = gme::gradient::CurveFactory::createCurve("linear", {});
    ASSERT_TRUE(curve_opt.has_value(), "lsp: linear curve created");

    auto fm = std::make_unique<gme::motion::FadeMotion>(
        "lsp_m1",
        "127.0.0.1:9995:/lsp",
        0, 1000.0f, 0.0f, 1.0f,
        std::move(*curve_opt),
        addr,
        "/lsp", "127.0.0.1", 9995,
        [](lo_address, const char*, float) -> int { return 0; });

    reg.addMotion(std::move(fm));
    ASSERT_TRUE(reg.size() == 1, "lsp: FadeMotion inserted");

    reg.setTickThreadContext(true);
    reg.tick(500);   // t=0.5, not complete
    ASSERT_TRUE(reg.size() == 1, "lsp: still active at t=0.5");

    reg.tick(1000);  // t=1.0, completes
    ASSERT_TRUE(reg.size() == 0, "lsp: removed on completion");

    gme::signal::StatusEmitRequest r;
    while (sq.pop(r)) emitted.push_back(r);
    bool found = false;
    for (auto& e : emitted)
        if (e.kind == gme::signal::StatusKind::MotionComplete && e.fade_id == "lsp_m1")
            found = true;
    ASSERT_TRUE(found, "lsp: MotionComplete emitted for FadeMotion");
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        {"add_motion_inserts",               test_add_motion_inserts},
        {"add_supersede",                    test_add_supersede},
        {"add_duplicate_id_rejects",         test_add_duplicate_id_rejects},
        {"add_duplicate_id_same_path",       test_add_duplicate_id_same_path_rejects},
        {"cancel_snap_calls_snap",           test_cancel_snap_calls_snap},
        {"cancel_hold_no_snap",              test_cancel_hold_no_snap},
        {"cancel_all_no_snap",               test_cancel_all_no_snap},
        {"tick_removes_completed",           test_tick_removes_completed},
        {"tick_osc_failure_threshold",       test_tick_osc_failure_threshold},
        {"tick_failure_reset_on_success",    test_tick_failure_reset_on_success},
        {"status_queue_overflow",            test_status_queue_overflow},
        {"status_routing_drain_context",     test_status_routing_drain_context},
        {"status_routing_tick_context",      test_status_routing_tick_context},
        {"lsp_fade_motion_through_registry", test_lsp_fade_motion_through_registry},
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
