/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file test_fade_motion.cpp
 * @brief Behavioural regression tests for the FadeMotion + MotionRegistry stack.
 *
 * Port of test_fade_registry.cpp (US1–US5, 20 tests). All assertions are
 * preserved verbatim; the only changes are:
 *  - `FadeRegistry` → `MotionRegistry` (via MotionFactory internally).
 *  - `StatusKind::FadeComplete` / `FadeError` → `MotionComplete` / `MotionError`.
 *  - `cancelFade` → `cancelMotion`.
 *  - `kOscFailureThreshold` accessed on `MotionRegistry`.
 *  - `StatusEmitRequest` → local `StatusEvent` struct (NNG queue removed).
 *  - `fade_id` → `motion_id`.
 *
 * ## Test infrastructure
 *
 * Tests use:
 *  - A `MtcTickSource` created without calling `start()` (`getMtcMs()` = 0).
 *  - A lambda capturing `emitted` as the `statusDirect` callback.
 *  - An injectable `OscSendFn` to simulate send failures (US4).
 *  - OSC sends to 127.0.0.1:9998 (unused port; UDP fire-and-forget).
 */

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "motion/MotionRegistry.h"
#include "signal/FadeCommand.h"
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

// Local status event type (replaces StatusEmitRequest after NNG removal)
struct StatusEvent {
    gme::signal::StatusKind kind = gme::signal::StatusKind::MotionComplete;
    std::string motion_id;
    std::string reason;
};

// ---------------------------------------------------------------------------
// Test fixture helpers
// ---------------------------------------------------------------------------

struct TestCtx {
    gme::time::MtcTickSource  tickSrc;
    std::vector<StatusEvent>  emitted;

    TestCtx() {}

    std::unique_ptr<gme::motion::MotionRegistry> makeReg() {
        return std::make_unique<gme::motion::MotionRegistry>(
            tickSrc,
            [this](gme::signal::StatusKind k,
                   const std::string& id,
                   const std::string& reason) {
                emitted.push_back({k, id, reason});
            });
    }

    std::unique_ptr<gme::motion::MotionRegistry> makeRegWithSend(
        gme::motion::MotionRegistry::OscSendFn fn)
    {
        return std::make_unique<gme::motion::MotionRegistry>(
            tickSrc,
            [this](gme::signal::StatusKind k,
                   const std::string& id,
                   const std::string& reason) {
                emitted.push_back({k, id, reason});
            },
            fn);
    }

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
        cmd.motion_id    = id;
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

static bool test_us1_sigmoid_accuracy() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    auto cmd = TestCtx::makeCmd("f1", "sigmoid", 0.0f, 1.0f, 2000.0f, 0);
    reg->apply(cmd);
    ASSERT_TRUE(reg->size() == 1, "us1_sigmoid: fade registered");

    float captured = -1.0f;
    auto sendFn = [&captured](lo_address, const char*, float v) -> int {
        captured = v;
        return 0;
    };
    auto reg2 = ctx.makeRegWithSend(sendFn);
    reg2->apply(cmd);
    reg2->tick(1000);
    ASSERT_NEAR(captured, 0.5f, 0.005f, "us1_sigmoid_accuracy at t=0.5");
    return true;
}

static bool test_us1_boundary_values() {
    TestCtx ctx;
    float captured = -99.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("f1", "linear", 0.0f, 1.0f, 1000.0f, 0);
    reg->apply(cmd);

    reg->tick(0);
    ASSERT_NEAR(captured, 0.0f, 1e-4f, "us1_boundary t=0");

    reg->tick(1000);
    ASSERT_NEAR(captured, 1.0f, 1e-4f, "us1_boundary t=1");
    return true;
}

static bool test_us1_zero_duration() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("f1", "linear", 0.0f, 0.8f, 0.0f, 0);
    reg->apply(cmd);
    ASSERT_TRUE(reg->size() == 1, "us1_zero_dur: registered");

    reg->tick(0);
    ASSERT_NEAR(captured, 0.8f, 1e-4f, "us1_zero_duration end_value sent");

    bool got_complete = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::MotionComplete && r.motion_id == "f1")
            got_complete = true;
    ASSERT_TRUE(got_complete, "us1_zero_duration MotionComplete emitted");
    ASSERT_TRUE(reg->size() == 0, "us1_zero_duration removed after completion");
    return true;
}

static bool test_us1_completion_and_removal() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("f1", "linear", 0.0f, 1.0f, 1000.0f, 0);
    reg->apply(cmd);

    reg->tick(500);
    reg->tick(1000);

    ASSERT_TRUE(!vals.empty(), "us1_completion: values sent");
    ASSERT_NEAR(vals.back(), 1.0f, 1e-4f, "us1_completion: final value = end_value");
    ASSERT_TRUE(reg->size() == 0, "us1_completion: fade removed");

    bool got_complete = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::MotionComplete && r.motion_id == "f1")
            got_complete = true;
    ASSERT_TRUE(got_complete, "us1_completion: MotionComplete emitted");
    return true;
}

static bool test_us1_rewind_below_start() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("f1", "linear", 0.2f, 1.0f, 2000.0f, 5000);
    reg->apply(cmd);

    reg->tick(3000);
    ASSERT_NEAR(captured, 0.2f, 1e-4f, "us1_rewind: value = start_value when t<0");
    ASSERT_TRUE(reg->size() == 1, "us1_rewind: fade still active");
    return true;
}

static bool test_us1_sentinel_resolution() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    gme::signal::FadeCommand cmd = TestCtx::makeCmd("f1", "linear", 0.0f, 1.0f, 1000.0f);
    cmd.start_mtc_ms = -1;
    reg->apply(cmd);

    reg->tick(500);
    ASSERT_NEAR(captured, 0.5f, 1e-4f, "us1_sentinel: resolved to getMtcMs()=0");
    return true;
}

// ---------------------------------------------------------------------------
// US2 — Motion Completion Status Notification
// ---------------------------------------------------------------------------

static bool test_us2_fade_complete_status() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    auto cmd = TestCtx::makeCmd("fade_x", "linear", 0.0f, 1.0f, 500.0f, 0);
    reg->apply(cmd);
    reg->tick(500);

    bool found = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::MotionComplete && r.motion_id == "fade_x")
            found = true;
    ASSERT_TRUE(found, "us2_fade_complete: MotionComplete emitted");
    return true;
}

static bool test_us2_status_timing() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    auto cmd = TestCtx::makeCmd("ftiming", "linear", 0.0f, 1.0f, 100.0f, 0);
    reg->apply(cmd);

    auto t0 = std::chrono::steady_clock::now();
    reg->tick(100);
    auto t1 = std::chrono::steady_clock::now();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    ASSERT_TRUE(us < 5000, "us2_timing: tick+emit < 5 ms");

    bool found = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::MotionComplete && r.motion_id == "ftiming")
            found = true;
    ASSERT_TRUE(found, "us2_timing: MotionComplete emitted in same tick");
    return true;
}

static bool test_us2_status_all_emitted() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    for (int i = 0; i < 70; ++i) {
        auto cmd = TestCtx::makeCmd(
            "fo" + std::to_string(i), "linear",
            0.0f, 1.0f, 0.0f,
            0, "127.0.0.1", 9997,
            "/over/" + std::to_string(i));
        reg->apply(cmd);
        reg->tick(0);
    }
    ASSERT_TRUE(ctx.emitted.size() == 70, "us2_all_emitted: all 70 statuses emitted");
    return true;
}

static bool test_us2_unknown_curve_type() {
    TestCtx ctx;
    auto reg = ctx.makeReg();

    auto cmd = TestCtx::makeCmd("bad_curve", "totally_unknown_xyz");
    reg->apply(cmd);

    ASSERT_TRUE(reg->size() == 0, "us2_unknown_curve: fade not registered");

    bool found = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::MotionError &&
            r.motion_id == "bad_curve" &&
            r.reason    == "unknown_curve_type")
            found = true;
    ASSERT_TRUE(found, "us2_unknown_curve: MotionError:unknown_curve_type emitted");
    return true;
}

static bool test_us2_supersede() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmdA = TestCtx::makeCmd("fadeA", "linear", 0.0f, 1.0f, 5000.0f, 0,
                                 "127.0.0.1", 9998, "/vol");
    reg->apply(cmdA);
    reg->tick(1000);  // fadeA at t=0.2 → sends 0.2

    auto cmdB = TestCtx::makeCmd("fadeB", "linear", 0.5f, 0.0f, 2000.0f, 1000,
                                 "127.0.0.1", 9998, "/vol");
    float fadeA_last_val = vals.empty() ? 0.0f : vals.back();
    reg->apply(cmdB);

    ASSERT_TRUE(reg->size() == 1, "us2_supersede: only one fade remains");

    vals.clear();
    reg->tick(1000);  // t=0.0 into fadeB → value = inherited last_sent_value
    ASSERT_TRUE(!vals.empty(), "us2_supersede: fadeB sends value at t=0");
    ASSERT_NEAR(vals[0], fadeA_last_val, 1e-4f,
                "us2_supersede: fadeB first value inherits fadeA last_sent_value");

    reg->tick(1500);
    ASSERT_TRUE(vals.size() > 1u, "us2_supersede: fadeB sends values");

    bool superseded_error = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::MotionError &&
            r.motion_id == "fadeA" && r.reason == "superseded")
            superseded_error = true;
    ASSERT_TRUE(superseded_error, "us2_supersede: MotionError:superseded for fadeA");
    return true;
}

// ---------------------------------------------------------------------------
// US3 — Cancel Motion with Snap or Hold
// ---------------------------------------------------------------------------

static bool test_us3_cancel_snap() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("fsnap", "linear", 0.0f, 0.9f, 5000.0f, 0);
    reg->apply(cmd);
    reg->tick(1000);
    vals.clear();

    reg->cancelMotion("fsnap", /*snap_to_end=*/true);

    ASSERT_TRUE(vals.size() == 1, "us3_snap: exactly one final OSC sent");
    ASSERT_NEAR(vals[0], 0.9f, 1e-4f, "us3_snap: final value = end_value");
    ASSERT_TRUE(reg->size() == 0, "us3_snap: fade removed");
    return true;
}

static bool test_us3_cancel_hold() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("fhold", "linear", 0.0f, 1.0f, 5000.0f, 0);
    reg->apply(cmd);
    reg->tick(1000);
    vals.clear();

    reg->cancelMotion("fhold", /*snap_to_end=*/false);

    ASSERT_TRUE(vals.empty(), "us3_hold: no extra OSC sent");
    ASSERT_TRUE(reg->size() == 0, "us3_hold: fade removed");
    return true;
}

static bool test_us3_cancel_all() {
    TestCtx ctx;
    std::vector<float> vals;
    auto send = [&](lo_address, const char*, float v) -> int { vals.push_back(v); return 0; };
    auto reg = ctx.makeRegWithSend(send);

    for (int i = 0; i < 3; ++i) {
        auto cmd = TestCtx::makeCmd("f" + std::to_string(i), "linear",
                                   0.0f, 1.0f, 5000.0f, 0,
                                   "127.0.0.1", 9998, "/gain" + std::to_string(i));
        reg->apply(cmd);
    }
    reg->tick(1000);
    vals.clear();

    reg->cancelAll();

    ASSERT_TRUE(vals.empty(),      "us3_cancel_all: no final OSC");
    ASSERT_TRUE(reg->size() == 0,  "us3_cancel_all: all fades removed");
    return true;
}

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

static bool test_us4_osc_failure_threshold() {
    TestCtx ctx;
    int send_count = 0;
    auto send = [&](lo_address, const char*, float) -> int {
        ++send_count;
        return -1;
    };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("ffail", "linear", 0.0f, 1.0f, 10000.0f, 0);
    reg->apply(cmd);

    for (int i = 0; i < gme::motion::MotionRegistry::kOscFailureThreshold; ++i) {
        reg->tick(i * 100);
    }

    ASSERT_TRUE(reg->size() == 0, "us4_threshold: fade removed after failures");

    bool found = false;
    for (auto& r : ctx.emitted)
        if (r.kind == gme::signal::StatusKind::MotionError &&
            r.motion_id == "ffail" && r.reason == "osc_send_failed")
            found = true;
    ASSERT_TRUE(found, "us4_threshold: MotionError:osc_send_failed emitted");
    return true;
}

static bool test_us4_transient_failure_recovery() {
    TestCtx ctx;
    int tick_n = 0;
    auto send = [&](lo_address, const char*, float) -> int {
        ++tick_n;
        return (tick_n <= 3) ? -1 : 0;
    };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("ftrans", "linear", 0.0f, 1.0f, 10000.0f, 0);
    reg->apply(cmd);

    for (int i = 0; i < 4; ++i) reg->tick(i * 100);

    ASSERT_TRUE(reg->size() == 1, "us4_recovery: fade alive after transient failures");

    for (auto& r : ctx.emitted)
        ASSERT_TRUE(r.reason != "osc_send_failed",
                    "us4_recovery: no osc_send_failed error");
    return true;
}

// ---------------------------------------------------------------------------
// US5 — MTC Pause, Resume, Rewind Behavior
// ---------------------------------------------------------------------------

static bool test_us5_pause_no_ticks() {
    TestCtx ctx;
    int send_count = 0;
    auto send = [&](lo_address, const char*, float) -> int { ++send_count; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("fpause", "linear", 0.0f, 1.0f, 5000.0f, 0);
    reg->apply(cmd);

    ASSERT_TRUE(send_count == 0, "us5_pause: no OSC sent while paused");
    ASSERT_TRUE(reg->size() == 1, "us5_pause: fade still registered");
    return true;
}

static bool test_us5_resume_correct_value() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("fresume", "linear", 0.0f, 1.0f, 2000.0f, 0);
    reg->apply(cmd);

    reg->tick(1500);
    ASSERT_NEAR(captured, 0.75f, 0.005f, "us5_resume: value matches t=0.75");
    return true;
}

static bool test_us5_rewind_before_start() {
    TestCtx ctx;
    float captured = -1.0f;
    auto send = [&](lo_address, const char*, float v) -> int { captured = v; return 0; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("frewind", "linear", 0.3f, 1.0f, 2000.0f, 5000);
    reg->apply(cmd);

    reg->tick(3000);
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
        {"us1_sigmoid_accuracy",    test_us1_sigmoid_accuracy},
        {"us1_boundary_values",     test_us1_boundary_values},
        {"us1_zero_duration",       test_us1_zero_duration},
        {"us1_completion_removal",  test_us1_completion_and_removal},
        {"us1_rewind_below_start",  test_us1_rewind_below_start},
        {"us1_sentinel_resolution", test_us1_sentinel_resolution},
        {"us2_fade_complete_status",    test_us2_fade_complete_status},
        {"us2_status_timing",           test_us2_status_timing},
        {"us2_status_all_emitted",      test_us2_status_all_emitted},
        {"us2_unknown_curve_type",      test_us2_unknown_curve_type},
        {"us2_supersede",               test_us2_supersede},
        {"us3_cancel_snap",          test_us3_cancel_snap},
        {"us3_cancel_hold",          test_us3_cancel_hold},
        {"us3_cancel_all",           test_us3_cancel_all},
        {"us3_cancel_all_timing",    test_us3_cancel_all_timing},
        {"us4_failure_threshold",        test_us4_osc_failure_threshold},
        {"us4_transient_recovery",       test_us4_transient_failure_recovery},
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
