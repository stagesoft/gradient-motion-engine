/**
 * @file test_fade_registry.cpp
 * @brief Unit tests for gme::engine::FadeRegistry (Phase 4).
 *
 * Tests are organized by user story. Each test function returns true on success
 * and false (printing to stderr) on failure. main() runs all tests and returns
 * 0 if all pass, 1 otherwise.
 *
 * ## Test infrastructure
 *
 * Tests use:
 *  - A `MtcTickSource` created without calling `start()` (no MIDI port opened).
 *    `getMtcMs()` returns 0 by default.
 *  - A `LockFreeQueue<StatusEmitRequest, 64>` as the status queue.
 *  - A lambda as the `statusDirect_` callback (records calls into a vector).
 *  - An injectable `OscSendFn` to simulate send failures (US4).
 *  - OSC sends to 127.0.0.1:9998 (unused port). On loopback UDP, `lo_send`
 *    returns 0 even if nobody is listening (fire-and-forget semantics).
 */

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "engine/FadeRegistry.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"
#include "signal/StatusEmitRequest.h"
#include "time/MtcTickSource.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool nearly(float a, float b, float tol = 0.005f) {
    return std::fabs(a - b) <= tol;
}

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { std::fprintf(stderr, "FAIL [%s]: %s\n", msg, #cond); return false; } } while(0)

#define ASSERT_NEAR(a, b, tol, msg) \
    do { if (!nearly((float)(a),(float)(b),(float)(tol))) { \
        std::fprintf(stderr, "FAIL [%s]: %.6f != %.6f (tol %.4f)\n", msg, (double)(a),(double)(b),(double)(tol)); \
        return false; } } while(0)

// ---------------------------------------------------------------------------
// Test fixture helpers
// ---------------------------------------------------------------------------

struct TestCtx {
    gme::time::MtcTickSource                                       tickSrc;
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> sq;
    std::vector<gme::signal::StatusEmitRequest>                    emitted;
    std::function<void(gme::signal::StatusKind, const std::string&, const std::string&)> direct;

    TestCtx() {
        direct = [this](gme::signal::StatusKind k,
                        const std::string& id,
                        const std::string& reason) {
            gme::signal::StatusEmitRequest r;
            r.kind    = k;
            r.fade_id = id;
            r.reason  = reason;
            emitted.push_back(r);
        };
    }

    /** Drain statusQueue into emitted. */
    void drainQueue() {
        gme::signal::StatusEmitRequest r;
        while (sq.pop(r)) emitted.push_back(r);
    }

    /** Build a registry with default (real) OSC send. */
    std::unique_ptr<gme::engine::FadeRegistry> makeReg() {
        return std::make_unique<gme::engine::FadeRegistry>(tickSrc, sq, direct);
    }

    /** Build a registry with injected OSC send function. */
    std::unique_ptr<gme::engine::FadeRegistry> makeRegWithSend(
        gme::engine::FadeRegistry::OscSendFn fn)
    {
        return std::make_unique<gme::engine::FadeRegistry>(tickSrc, sq, direct, fn);
    }

    /** Build a START_FADE command. */
    static gme::signal::FadeCommand makeCmd(
        const std::string& id,
        const std::string& curve_type = "linear",
        float start_v = 0.0f, float end_v = 1.0f,
        float duration = 3000.0f,
        long  start_mtc = 0,
        const std::string& host = "127.0.0.1",
        int   port = 9998,
        const std::string& path = "/test/gain")
    {
        gme::signal::FadeCommand cmd;
        cmd.type         = gme::signal::FadeCommand::Type::START_FADE;
        cmd.fade_id      = id;
        cmd.curve_type   = curve_type;
        cmd.start_value  = start_v;
        cmd.end_value    = end_v;
        cmd.duration_ms  = duration;
        cmd.start_mtc_ms = start_mtc;
        cmd.osc_host     = host;
        cmd.osc_port     = port;
        cmd.osc_path     = path;
        return cmd;
    }
};

// ---------------------------------------------------------------------------
// US1 — MTC-Driven Fade Execution
// ---------------------------------------------------------------------------

/** SC-001: curve value at t=0.5 matches expected within ±0.005. */
static bool test_us1_sigmoid_accuracy() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    // Sigmoid with default params: midpoint=0.5, steepness=8
    auto cmd = TestCtx::makeCmd("f1", "sigmoid", 0.0f, 1.0f, 2000.0f, 0);
    reg->apply(cmd);
    ASSERT_TRUE(reg->size() == 1, "us1_sigmoid: fade registered");

    // At t=0.5 (mtc_ms = 1000):
    // sigmoid(0.5) with steepness=8 ≈ 0.5 (symmetric)
    float captured = -1.0f;
    auto sendFn = [&captured](lo_address, const char*, float v) -> int {
        captured = v;
        return 0;
    };
    auto reg2 = ctx.makeRegWithSend(sendFn);
    reg2->apply(cmd);
    reg2->tick(1000);  // t = 1000/2000 = 0.5
    ASSERT_NEAR(captured, 0.5f, 0.005f, "us1_sigmoid_accuracy at t=0.5");
    return true;
}

/** SC-001: curve boundary values at t=0 and t=1. */
static bool test_us1_boundary_values() {
    TestCtx ctx;
    float captured = -99.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("f1", "linear", 0.0f, 1.0f, 1000.0f, 0);
    reg->apply(cmd);

    reg->tick(0);       // t=0 → value=0
    ASSERT_NEAR(captured, 0.0f, 1e-4f, "us1_boundary t=0");

    // Re-register (t=0 version was the first tick, fade not done yet)
    reg->tick(1000);    // t=1 → value=1
    ASSERT_NEAR(captured, 1.0f, 1e-4f, "us1_boundary t=1");
    return true;
}

/** duration_ms == 0 → fade completes immediately on first tick with end_value. */
static bool test_us1_zero_duration() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("f1", "linear", 0.0f, 0.8f, /*duration=*/0.0f, 0);
    reg->apply(cmd);
    ASSERT_TRUE(reg->size() == 1, "us1_zero_dur: registered");

    reg->tick(0);
    ASSERT_NEAR(captured, 0.8f, 1e-4f, "us1_zero_duration end_value sent");

    ctx.drainQueue();
    bool got_complete = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::FadeComplete && r.fade_id == "f1")
            got_complete = true;
    ASSERT_TRUE(got_complete, "us1_zero_duration FadeComplete emitted");
    ASSERT_TRUE(reg->size() == 0, "us1_zero_duration removed after completion");
    return true;
}

/** Fade completes at t=1.0: final OSC is end_value, fade removed. */
static bool test_us1_completion_and_removal() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("f1", "linear", 0.0f, 1.0f, 1000.0f, 0);
    reg->apply(cmd);

    reg->tick(500);   // t=0.5
    reg->tick(1000);  // t=1.0 — completes

    ASSERT_TRUE(!vals.empty(), "us1_completion: values sent");
    ASSERT_NEAR(vals.back(), 1.0f, 1e-4f, "us1_completion: final value = end_value");
    ASSERT_TRUE(reg->size() == 0, "us1_completion: fade removed");

    ctx.drainQueue();
    bool got_complete = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::FadeComplete && r.fade_id == "f1")
            got_complete = true;
    ASSERT_TRUE(got_complete, "us1_completion: FadeComplete emitted");
    return true;
}

/** Rewind before start_mtc_ms → value = start_value (FR-001 clamp). */
static bool test_us1_rewind_below_start() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    // Fade starts at mtc=5000
    auto cmd = TestCtx::makeCmd("f1", "linear", 0.2f, 1.0f, 2000.0f, 5000);
    reg->apply(cmd);

    reg->tick(3000);  // before start_mtc_ms
    ASSERT_NEAR(captured, 0.2f, 1e-4f, "us1_rewind: value = start_value when t<0");
    ASSERT_TRUE(reg->size() == 1, "us1_rewind: fade still active");
    return true;
}

/** FR-016: start_mtc_ms == -1 → resolved to getMtcMs() (=0 in test context). */
static bool test_us1_sentinel_resolution() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    // start_mtc_ms = -1 (use makeCmd default which sets 0, override here)
    gme::signal::FadeCommand cmd = TestCtx::makeCmd("f1", "linear", 0.0f, 1.0f, 1000.0f);
    cmd.start_mtc_ms = -1;  // sentinel
    reg->apply(cmd);

    // getMtcMs() returns 0 → start_mtc_ms should be resolved to 0
    reg->tick(500);  // t = 500/1000 = 0.5 → linear value = 0.5
    ASSERT_NEAR(captured, 0.5f, 1e-4f, "us1_sentinel: resolved to getMtcMs()=0");
    return true;
}

// ---------------------------------------------------------------------------
// US2 — Fade Completion Status Notification
// ---------------------------------------------------------------------------

/** FadeComplete is pushed to statusQueue on tick completion. */
static bool test_us2_fade_complete_status() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    auto cmd = TestCtx::makeCmd("fade_x", "linear", 0.0f, 1.0f, 500.0f, 0);
    reg->apply(cmd);
    reg->tick(500);  // completes

    ctx.drainQueue();
    bool found = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::FadeComplete && r.fade_id == "fade_x")
            found = true;
    ASSERT_TRUE(found, "us2_fade_complete: FadeComplete in status queue");
    return true;
}

/** SC-003: FadeComplete arrives within 250 ms of completion (queue-based timing). */
static bool test_us2_status_timing() {
    // The statusQueue is drained by a worker thread in the real daemon.
    // Here we verify the item is *enqueued* immediately (within the same tick).
    TestCtx ctx;
    auto reg = ctx.makeReg();

    auto cmd = TestCtx::makeCmd("ftiming", "linear", 0.0f, 1.0f, 100.0f, 0);
    reg->apply(cmd);

    auto t0 = std::chrono::steady_clock::now();
    reg->tick(100);  // completes
    auto t1 = std::chrono::steady_clock::now();

    // The enqueue itself must be nearly instant (in the tick call)
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    ASSERT_TRUE(us < 5000, "us2_timing: tick+enqueue < 5 ms");

    ctx.drainQueue();
    bool found = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::FadeComplete && r.fade_id == "ftiming")
            found = true;
    ASSERT_TRUE(found, "us2_timing: FadeComplete enqueued in same tick");
    return true;
}

/** Status queue drop-oldest on overflow: tick thread never blocks. */
static bool test_us2_status_queue_overflow() {
    TestCtx ctx;
    // Inject a send function that always succeeds
    auto reg = ctx.makeReg();

    // Flood the 64-slot status queue: register + complete 70 one-tick fades
    // (all complete immediately with duration=0 on different paths)
    for (int i = 0; i < 70; ++i) {
        auto cmd = TestCtx::makeCmd(
            "fo" + std::to_string(i), "linear",
            0.0f, 1.0f, 0.0f,          // duration=0 → instant complete
            0,
            "127.0.0.1", 9997,
            "/over/" + std::to_string(i));
        reg->apply(cmd);
        reg->tick(0);
    }
    // Drain queue — should have at most 64 items (older ones were dropped)
    ctx.drainQueue();
    ASSERT_TRUE(ctx.emitted.size() <= 64, "us2_overflow: queue capped at 64");
    ASSERT_TRUE(ctx.emitted.size() > 0,   "us2_overflow: some statuses emitted");
    return true;
}

/** Unknown curve type → FadeError:"unknown_curve_type" emitted, fade not registered. */
static bool test_us2_unknown_curve_type() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    auto cmd = TestCtx::makeCmd("bad_curve", "totally_unknown_xyz");
    reg->apply(cmd);

    ASSERT_TRUE(reg->size() == 0, "us2_unknown_curve: fade not registered");

    ctx.drainQueue();
    bool found = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::FadeError &&
            r.fade_id == "bad_curve" &&
            r.reason  == "unknown_curve_type")
            found = true;
    ASSERT_TRUE(found, "us2_unknown_curve: FadeError:unknown_curve_type emitted");
    return true;
}

/** FR-014 supersede: new fade on same OSC target cancels prior without final OSC. */
static bool test_us2_supersede() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    // Register fade A
    auto cmdA = TestCtx::makeCmd("fadeA", "linear", 0.0f, 1.0f, 5000.0f, 0,
                                 "127.0.0.1", 9998, "/vol");
    reg->apply(cmdA);
    reg->tick(1000);  // partial progress — fadeA at t=0.2 → sends 0.2

    // Register fade B on the same OSC target — should supersede A.
    // fadeA's last_sent_value at tick(1000) is linear(t=0.2) = 0.2.
    // fadeB's raw start_value is 0.5, but the registry must inherit 0.2
    // so there is no jump in the OSC stream.
    auto cmdB = TestCtx::makeCmd("fadeB", "linear", 0.5f, 0.0f, 2000.0f, 1000,
                                 "127.0.0.1", 9998, "/vol");
    float fadeA_last_val = vals.empty() ? 0.0f : vals.back();  // 0.2 from tick(1000)
    reg->apply(cmdB);

    // Only fadeB should be in registry
    ASSERT_TRUE(reg->size() == 1, "us2_supersede: only one fade remains");

    // Tick fadeB at its start time (t=0) — first OSC value must equal the
    // inherited last_sent_value of fadeA, not fadeB's raw start_value (0.5).
    vals.clear();
    reg->tick(1000);  // t=0.0 into fadeB → value = effective_start = 0.2
    ASSERT_TRUE(!vals.empty(), "us2_supersede: fadeB sends value at t=0");
    ASSERT_NEAR(vals[0], fadeA_last_val, 1e-4f,
                "us2_supersede: fadeB first value inherits fadeA last_sent_value");

    reg->tick(1500);  // tick at 500 ms into fadeB
    ASSERT_TRUE(vals.size() > 1u, "us2_supersede: fadeB sends values");

    ctx.drainQueue();
    bool superseded_error = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::FadeError &&
            r.fade_id == "fadeA" && r.reason == "superseded")
            superseded_error = true;
    ASSERT_TRUE(superseded_error, "us2_supersede: FadeError:superseded for fadeA");
    return true;
}

// ---------------------------------------------------------------------------
// US3 — Cancel Fade with Snap or Hold
// ---------------------------------------------------------------------------

/** cancelFade(snap_to_end=true) → one final OSC at end_value, fade removed. */
static bool test_us3_cancel_snap() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("fsnap", "linear", 0.0f, 0.9f, 5000.0f, 0);
    reg->apply(cmd);
    reg->tick(1000);  // t=0.2
    vals.clear();

    reg->cancelFade("fsnap", /*snap_to_end=*/true);

    ASSERT_TRUE(vals.size() == 1, "us3_snap: exactly one final OSC sent");
    ASSERT_NEAR(vals[0], 0.9f, 1e-4f, "us3_snap: final value = end_value");
    ASSERT_TRUE(reg->size() == 0, "us3_snap: fade removed");
    return true;
}

/** cancelFade(snap_to_end=false) → no extra OSC, fade removed. */
static bool test_us3_cancel_hold() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("fhold", "linear", 0.0f, 1.0f, 5000.0f, 0);
    reg->apply(cmd);
    reg->tick(1000);
    vals.clear();

    reg->cancelFade("fhold", /*snap_to_end=*/false);

    ASSERT_TRUE(vals.empty(), "us3_hold: no extra OSC sent");
    ASSERT_TRUE(reg->size() == 0, "us3_hold: fade removed");
    return true;
}

/** cancelAll() removes all fades without sending any final OSC values. */
static bool test_us3_cancel_all() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    // Register 3 fades on distinct OSC paths
    for (int i = 0; i < 3; ++i) {
        auto cmd = TestCtx::makeCmd("f" + std::to_string(i), "linear",
                                   0.0f, 1.0f, 5000.0f, 0,
                                   "127.0.0.1", 9998, "/gain" + std::to_string(i));
        reg->apply(cmd);
    }
    reg->tick(1000);
    vals.clear();

    reg->cancelAll();  // SC-004: must complete within one tick cycle

    ASSERT_TRUE(vals.empty(),      "us3_cancel_all: no final OSC");
    ASSERT_TRUE(reg->size() == 0,  "us3_cancel_all: all fades removed");
    return true;
}

/** cancelAll() completes quickly (SC-004: ≤5 ms). */
static bool test_us3_cancel_all_timing() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    for (int i = 0; i < 50; ++i) {
        auto cmd = TestCtx::makeCmd("f" + std::to_string(i), "linear",
                                   0.0f, 1.0f, 5000.0f, 0,
                                   "127.0.0.1", 9998, "/p" + std::to_string(i));
        reg->apply(cmd);
    }

    auto t0 = std::chrono::steady_clock::now();
    reg->cancelAll();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    ASSERT_TRUE(ms < 5, "us3_cancel_all_timing: completes within 5 ms (SC-004)");
    return true;
}

// ---------------------------------------------------------------------------
// US4 — OSC Send Failure Reporting
// ---------------------------------------------------------------------------

/** After kOscFailureThreshold consecutive failures → FadeError:osc_send_failed. */
static bool test_us4_osc_failure_threshold() {
    TestCtx ctx;
    int send_count = 0;
    auto send = [&](lo_address, const char*, float) -> int {
        ++send_count;
        return -1;  // always fail
    };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("ffail", "linear", 0.0f, 1.0f, 10000.0f, 0);
    reg->apply(cmd);

    // Tick kOscFailureThreshold times — on the last failure fade is removed
    for (int i = 0; i < gme::engine::FadeRegistry::kOscFailureThreshold; ++i) {
        reg->tick(i * 100);
    }

    ASSERT_TRUE(reg->size() == 0, "us4_threshold: fade removed after failures");

    ctx.drainQueue();
    bool found = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::FadeError &&
            r.fade_id == "ffail" && r.reason == "osc_send_failed")
            found = true;
    ASSERT_TRUE(found, "us4_threshold: FadeError:osc_send_failed emitted");
    return true;
}

/** Transient failures (< threshold) do NOT kill the fade (SC-006). */
static bool test_us4_transient_failure_recovery() {
    TestCtx ctx;
    int tick_n = 0;
    auto send = [&](lo_address, const char*, float) -> int {
        ++tick_n;
        // Fail for 3 ticks, then succeed
        return (tick_n <= 3) ? -1 : 0;
    };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("ftrans", "linear", 0.0f, 1.0f, 10000.0f, 0);
    reg->apply(cmd);

    // 4 ticks: 3 failures then 1 success — should NOT kill the fade
    for (int i = 0; i < 4; ++i) reg->tick(i * 100);

    ASSERT_TRUE(reg->size() == 1, "us4_recovery: fade alive after transient failures");

    ctx.drainQueue();
    for (auto& r : ctx.emitted)
        ASSERT_TRUE(r.reason != "osc_send_failed",
                    "us4_recovery: no osc_send_failed error");
    return true;
}

// ---------------------------------------------------------------------------
// US5 — MTC Pause, Resume, Rewind Behavior
// ---------------------------------------------------------------------------

/** Pause: no tick calls → no OSC messages. */
static bool test_us5_pause_no_ticks() {
    TestCtx ctx;
    int send_count = 0;
    auto send = [&](lo_address, const char*, float) -> int { ++send_count; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("fpause", "linear", 0.0f, 1.0f, 5000.0f, 0);
    reg->apply(cmd);
    // No tick() calls — simulates MTC stopped

    ASSERT_TRUE(send_count == 0, "us5_pause: no OSC sent while paused");
    ASSERT_TRUE(reg->size() == 1, "us5_pause: fade still registered");
    return true;
}

/** Resume: after pause, first tick at T produces curve.evaluate(t at T). */
static bool test_us5_resume_correct_value() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("fresume", "linear", 0.0f, 1.0f, 2000.0f, 0);
    reg->apply(cmd);

    // "Resume" at t=0.75 (mtc=1500)
    reg->tick(1500);
    ASSERT_NEAR(captured, 0.75f, 0.005f, "us5_resume: value matches t=0.75");
    return true;
}

/** Rewind before start_mtc_ms → OSC value = start_value (FR-001 clamp). */
static bool test_us5_rewind_before_start() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("frewind", "linear", 0.3f, 1.0f, 2000.0f, 5000);
    reg->apply(cmd);

    reg->tick(3000);  // before start_mtc_ms=5000, t < 0 → clamped to 0 → start_value
    ASSERT_NEAR(captured, 0.3f, 1e-4f, "us5_rewind: value = start_value");
    ASSERT_TRUE(reg->size() == 1, "us5_rewind: fade still active");
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        // US1
        {"us1_sigmoid_accuracy",    test_us1_sigmoid_accuracy},
        {"us1_boundary_values",     test_us1_boundary_values},
        {"us1_zero_duration",       test_us1_zero_duration},
        {"us1_completion_removal",  test_us1_completion_and_removal},
        {"us1_rewind_below_start",  test_us1_rewind_below_start},
        {"us1_sentinel_resolution", test_us1_sentinel_resolution},
        // US2
        {"us2_fade_complete_status",    test_us2_fade_complete_status},
        {"us2_status_timing",           test_us2_status_timing},
        {"us2_status_queue_overflow",   test_us2_status_queue_overflow},
        {"us2_unknown_curve_type",      test_us2_unknown_curve_type},
        {"us2_supersede",               test_us2_supersede},
        // US3
        {"us3_cancel_snap",          test_us3_cancel_snap},
        {"us3_cancel_hold",          test_us3_cancel_hold},
        {"us3_cancel_all",           test_us3_cancel_all},
        {"us3_cancel_all_timing",    test_us3_cancel_all_timing},
        // US4
        {"us4_failure_threshold",        test_us4_osc_failure_threshold},
        {"us4_transient_recovery",       test_us4_transient_failure_recovery},
        // US5
        {"us5_pause_no_ticks",       test_us5_pause_no_ticks},
        {"us5_resume_correct_value", test_us5_resume_correct_value},
        {"us5_rewind_before_start",  test_us5_rewind_before_start},
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
