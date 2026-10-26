/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileContributor: Ion Reguera <ion@stagelab.coop>
 * ***
 */

/**
 * @file test_start_day_align.cpp
 * @brief Day alignment of explicit fade starts (StartDayAlign.h, MotionFactory).
 *
 * The -1 sentinel is resolved before any conversion; an explicit start is an
 * engine time converted into this process's MTC day; no clamp; the production
 * resolver follows mtcreceiver's head-authoritative flag.
 */

#include <cstdio>
#include <memory>
#include <string>

#include "motion/IMotion.h"
#include "motion/MotionFactory.h"
#include "motion/MotionRegistry.h"
#include "motion/StartDayAlign.h"
#include "signal/FadeCommand.h"
#include "time/MtcTickSource.h"
#include "mtcreceiver/mtcreceiver.h"

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { \
        std::fprintf(stderr, "FAIL [%s]: %s (line %d)\n", msg, #cond, __LINE__); \
        return false; \
    } } while(0)

using gme::motion::StartResolution;
using gme::motion::resolveStart;

static constexpr long DAY = MtcReceiver::DAY_MS;

struct FakeHead {
    bool authoritative = true;
    long head = 0;
    int calls = 0;
    bool operator()(long engineMs, long& localMs, long& k) {
        ++calls;
        if (!authoritative) return false;
        k = MtcReceiver::dayShift(engineMs, head);
        localMs = engineMs - k * DAY;
        return true;
    }
};

/** -1 = "now" (FR-016): resolved first, never handed to the conversion. */
static bool test_sentinel_before_conversion() {
    FakeHead f{true, DAY + 600000};
    long start = 0, k = 7;
    const auto how = resolveStart(-1, DAY + 600000, f, start, k);
    ASSERT_TRUE(how == StartResolution::Sentinel, "sentinel");
    ASSERT_TRUE(start == DAY + 600000, "sentinel = current MTC");
    ASSERT_TRUE(k == 0, "k = 0");
    ASSERT_TRUE(f.calls == 0, "-1 is not an engine time: no conversion");
    return true;
}

static bool test_aligned_unchanged() {
    FakeHead f{true, 13 * 3600000L};
    long start = 0, k = 7;
    ASSERT_TRUE(resolveStart(13 * 3600000L + 5000, 13 * 3600000L, f, start, k)
                == StartResolution::Converted, "converted");
    ASSERT_TRUE(k == 0 && start == 13 * 3600000L + 5000, "same day: unchanged");
    return true;
}

/** Daemon restarted past 24 h: its head is in day 0, the engine's in day 1. */
static bool test_engine_ahead_k1() {
    FakeHead f{true, 600000};
    long start = 0, k = 0;
    resolveStart(DAY + 605000, 600000, f, start, k);
    ASSERT_TRUE(k == 1, "k = 1");
    ASSERT_TRUE(start == 605000, "5 s ahead, not 24 h (no frozen fade)");
    return true;
}

/** Daemon alive across midnight, engine reset since: engine a day behind. */
static bool test_engine_behind_k_minus1() {
    FakeHead f{true, DAY + 600000};
    long start = 0, k = 0;
    resolveStart(605000, DAY + 600000, f, start, k);
    ASSERT_TRUE(k == -1, "k = -1");
    ASSERT_TRUE(start == DAY + 605000, "5 s ahead, not 24 h past (no instant cut)");
    return true;
}

/** No authoritative head: as received, the pre-fix behaviour. */
static bool test_not_authoritative_as_received() {
    FakeHead f{false, 0};
    long start = 0, k = 7;
    ASSERT_TRUE(resolveStart(DAY + 605000, 10, f, start, k) == StartResolution::AsReceived,
                "as received");
    ASSERT_TRUE(start == DAY + 605000 && k == 0, "raw start, k = 0");
    return true;
}

/** No clamp: a start just before midnight seen just after it is slightly negative. */
static bool test_no_clamp_at_the_wrap_edge() {
    FakeHead f{true, 10};
    long start = 0, k = 0;
    resolveStart(86399990L, 10, f, start, k);
    ASSERT_TRUE(k == 1, "k = 1");
    ASSERT_TRUE(start == -10, "kept at -10 ms, not clamped to 0");
    return true;
}

static bool test_rules() {
    ASSERT_TRUE(!gme::motion::implausibleStart(5000 + gme::motion::kHourMs, 5000), "1 h: ok");
    ASSERT_TRUE(gme::motion::implausibleStart(5001 + gme::motion::kHourMs, 5000), "> 1 h");
    ASSERT_TRUE(!gme::motion::realignIsWarning(1, 600000), "k = 1: info");
    ASSERT_TRUE(!gme::motion::realignIsWarning(-1, DAY + 1), "k = -1 after a wrap: info");
    ASSERT_TRUE(gme::motion::realignIsWarning(-1, 600000), "k = -1 in day 0: warning");
    return true;
}

/** The production resolver, MtcTickSource::toLocalDay, follows mtcreceiver's flag. */
static bool test_production_resolver() {
    MtcReceiver::resetStaticStateForTesting();
    gme::time::MtcTickSource src;
    MtcReceiver::mtcHead.store(600000);
    long local = -7, k = -7;
    ASSERT_TRUE(!src.toLocalDay(DAY + 605000, local, k), "not authoritative: false");
    ASSERT_TRUE(local == -7 && k == -7, "outputs untouched");
    MtcReceiver::setHeadAuthoritativeForTesting(true);
    ASSERT_TRUE(src.toLocalDay(DAY + 605000, local, k), "authoritative: converted");
    ASSERT_TRUE(k == 1 && local == 605000, "day 1 -> this day");
    MtcReceiver::resetStaticStateForTesting();
    return true;
}

/** End to end through MotionFactory::fromCommand. */
static bool test_factory_converts_explicit_start() {
    MtcReceiver::resetStaticStateForTesting();
    gme::time::MtcTickSource src;
    MtcReceiver::mtcHead.store(600000);
    MtcReceiver::setHeadAuthoritativeForTesting(true);
    gme::motion::MotionFactory::Context ctx{
        src,
        [](lo_address, const char*, float) { return 0; },
        [](gme::signal::StatusKind, const std::string&, const std::string&) {}
    };
    gme::signal::FadeCommand cmd;
    cmd.type = gme::signal::FadeCommand::Type::START_FADE;
    cmd.motion_id = "m1";
    cmd.osc_host = "127.0.0.1";
    cmd.osc_port = 9999;
    cmd.osc_path = "/volmaster";
    cmd.curve_type = "linear";
    cmd.duration_ms = 1000.0f;
    cmd.start_mtc_ms = DAY + 605000;
    auto m = gme::motion::MotionFactory::fromCommand(cmd, ctx);
    ASSERT_TRUE(m != nullptr, "motion built");
    ASSERT_TRUE(m->start_mtc_ms == 605000, "explicit start in this process's day");

    cmd.motion_id = "m2";
    cmd.start_mtc_ms = -1;
    auto s = gme::motion::MotionFactory::fromCommand(cmd, ctx);
    ASSERT_TRUE(s != nullptr && s->start_mtc_ms == 600000, "sentinel = current MTC");
    MtcReceiver::resetStaticStateForTesting();
    return true;
}

static gme::signal::FadeCommand fadeCmd(const std::string& id, long start) {
    gme::signal::FadeCommand cmd;
    cmd.type = gme::signal::FadeCommand::Type::START_FADE;
    cmd.motion_id = id;
    cmd.osc_host = "127.0.0.1";
    cmd.osc_port = 9999;
    cmd.osc_path = "/volmaster/" + id;
    cmd.curve_type = "linear";
    cmd.duration_ms = 1000.0f;
    cmd.start_mtc_ms = start;
    return cmd;
}

/** Factory, k = -1: daemon alive across midnight, engine reset since. */
static bool test_factory_engine_behind() {
    MtcReceiver::resetStaticStateForTesting();
    gme::time::MtcTickSource src;
    MtcReceiver::mtcHead.store(DAY + 600000);
    MtcReceiver::setHeadAuthoritativeForTesting(true);
    gme::motion::MotionFactory::Context ctx{
        src,
        [](lo_address, const char*, float) { return 0; },
        [](gme::signal::StatusKind, const std::string&, const std::string&) {}
    };
    auto cmd = fadeCmd("b1", 605000);
    auto m = gme::motion::MotionFactory::fromCommand(cmd, ctx);
    ASSERT_TRUE(m != nullptr && m->start_mtc_ms == DAY + 605000, "engine day 0 -> this day 1");
    MtcReceiver::resetStaticStateForTesting();
    return true;
}

/**
 * No authoritative head: START_FADE (explicit or -1) waits in the registry and
 * is built by the first tick that finds the head authoritative, in this day.
 */
static bool test_registry_defers_until_authoritative() {
    MtcReceiver::resetStaticStateForTesting();
    gme::time::MtcTickSource src;
    gme::motion::MotionRegistry reg(src,
        [](gme::signal::StatusKind, const std::string&, const std::string&) {});
    MtcReceiver::mtcHead.store(30);                       // a quarter-frame ramp, not a head
    auto a = fadeCmd("d1", DAY + 605000);
    auto b = fadeCmd("d2", -1);
    reg.apply(a);
    reg.apply(b);
    ASSERT_TRUE(reg.size() == 0, "nothing built on a ramp head");
    ASSERT_TRUE(reg.deferredCount() == 2, "both wait, the sentinel too");
    reg.tick(40);
    ASSERT_TRUE(reg.size() == 0 && reg.deferredCount() == 2, "still waiting");

    MtcReceiver::mtcHead.store(600000);                   // first complete frame
    MtcReceiver::setHeadAuthoritativeForTesting(true);
    reg.tick(600000);
    ASSERT_TRUE(reg.deferredCount() == 0, "retried");
    ASSERT_TRUE(reg.size() == 2, "both built once the head is authoritative");
    MtcReceiver::resetStaticStateForTesting();
    return true;
}

/** Cancels reach deferred fades; a re-sent motion_id replaces the waiting one. */
static bool test_registry_deferred_cancel_and_replace() {
    MtcReceiver::resetStaticStateForTesting();
    gme::time::MtcTickSource src;
    gme::motion::MotionRegistry reg(src,
        [](gme::signal::StatusKind, const std::string&, const std::string&) {});
    auto a = fadeCmd("c1", 5000);
    auto a2 = fadeCmd("c1", 6000);
    auto b = fadeCmd("c2", 5000);
    reg.apply(a);
    reg.apply(a2);
    reg.apply(b);
    ASSERT_TRUE(reg.deferredCount() == 2, "same motion_id: latest replaces");
    gme::signal::FadeCommand cancel;
    cancel.type = gme::signal::FadeCommand::Type::CANCEL_MOTION;
    cancel.motion_id = "c2";
    reg.apply(cancel);
    ASSERT_TRUE(reg.deferredCount() == 1, "CANCEL_MOTION removes a deferred fade");
    gme::signal::FadeCommand all;
    all.type = gme::signal::FadeCommand::Type::CANCEL_ALL;
    reg.apply(all);
    ASSERT_TRUE(reg.deferredCount() == 0, "CANCEL_ALL clears them");
    MtcReceiver::setHeadAuthoritativeForTesting(true);
    reg.tick(5000);
    ASSERT_TRUE(reg.size() == 0, "a cancelled fade is never built");
    MtcReceiver::resetStaticStateForTesting();
    return true;
}

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        {"sentinel_before_conversion",    test_sentinel_before_conversion},
        {"aligned_unchanged",             test_aligned_unchanged},
        {"engine_ahead_k1",               test_engine_ahead_k1},
        {"engine_behind_k_minus1",        test_engine_behind_k_minus1},
        {"not_authoritative_as_received", test_not_authoritative_as_received},
        {"no_clamp_at_the_wrap_edge",     test_no_clamp_at_the_wrap_edge},
        {"rules",                         test_rules},
        {"production_resolver",           test_production_resolver},
        {"factory_converts_explicit_start", test_factory_converts_explicit_start},
        {"factory_engine_behind",         test_factory_engine_behind},
        {"registry_defers_until_authoritative", test_registry_defers_until_authoritative},
        {"registry_deferred_cancel_and_replace", test_registry_deferred_cancel_and_replace},
    };
    int passed = 0, failed = 0;
    for (auto& t : tests) {
        bool ok = t.fn();
        std::fprintf(stdout, "  %s  %s\n", ok ? "PASS" : "FAIL", t.name);
        ok ? ++passed : ++failed;
    }
    std::fprintf(stdout, "\n%d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
