/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file test_osc_parse.cpp
 * @brief Unit tests for parseFadeOscCommand — field-level parsing, type-tag
 *        validation, node_name filter, and field validation rules.
 *
 * All tests are synthetic: they construct lo_arg arrays in-process without
 * any network I/O.
 *
 * ## Addresses under test
 *
 *  - `/gradient/start_fade`    type tag `sssisffhiss` (11 args)
 *  - `/gradient/cancel_motion` type tag `ss`           (2 args)
 *  - `/gradient/cancel_all`    type tag `s`            (1 arg)
 */

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <lo/lo.h>
#include <nlohmann/json.hpp>

#include "signal/FadeCommand.h"
#include "signal/parseFadeOscCommand.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { \
        std::fprintf(stderr, "FAIL [%s]: %s (line %d)\n", msg, #cond, __LINE__); \
        return false; \
    } } while(0)

#define ASSERT_EQ_INT(a, b, msg) \
    do { if ((a) != (b)) { \
        std::fprintf(stderr, "FAIL [%s]: %d != %d (line %d)\n", msg, (int)(a), (int)(b), __LINE__); \
        return false; \
    } } while(0)

#define ASSERT_EQ_STR(a, b, msg) \
    do { if (std::string(a) != std::string(b)) { \
        std::fprintf(stderr, "FAIL [%s]: '%s' != '%s' (line %d)\n", msg, (a), (b), __LINE__); \
        return false; \
    } } while(0)

using PR = gme::signal::ParseResult;

// ---------------------------------------------------------------------------
// lo_arg builder helpers
// ---------------------------------------------------------------------------

// Holds string storage alive alongside the lo_arg union.
// args is reserved upfront so push_back never reallocates (stable ptrs).
struct ArgStore {
    std::vector<std::string> strings;
    std::vector<lo_arg>      args;
    std::vector<lo_arg*>     ptrs;

    ArgStore() {
        strings.reserve(16);
        args.reserve(16);   // prevent reallocation; ptrs into args must stay valid
        ptrs.reserve(16);
    }

    void addString(const std::string& s) {
        strings.push_back(s);
        // liblo string arg: argv[i] points directly to the char buffer
        ptrs.push_back(reinterpret_cast<lo_arg*>(
            const_cast<char*>(strings.back().c_str())));
    }

    void addInt32(int32_t v) {
        args.push_back({});
        args.back().i = v;
        ptrs.push_back(&args.back());
    }

    void addFloat(float v) {
        args.push_back({});
        args.back().f = v;
        ptrs.push_back(&args.back());
    }

    void addInt64(int64_t v) {
        args.push_back({});
        args.back().h = v;
        ptrs.push_back(&args.back());
    }

    lo_arg** argv() { return ptrs.data(); }
    int      argc() { return static_cast<int>(ptrs.size()); }
};

// Build a well-formed start_fade ArgStore
static ArgStore makeStartFadeArgs(
    const std::string& motion_id    = "fade-001",
    const std::string& node_name    = "node-A",
    const std::string& osc_host     = "127.0.0.1",
    int32_t            osc_port     = 9001,
    const std::string& osc_path     = "/vol",
    float              start_value  = 0.0f,
    float              end_value    = 1.0f,
    int64_t            start_mtc_ms = 0,
    int32_t            duration_ms  = 1000,
    const std::string& curve_type   = "linear",
    const std::string& curve_json   = "{}")
{
    ArgStore s;
    s.addString(motion_id);
    s.addString(node_name);
    s.addString(osc_host);
    s.addInt32(osc_port);
    s.addString(osc_path);
    s.addFloat(start_value);
    s.addFloat(end_value);
    s.addInt64(start_mtc_ms);
    s.addInt32(duration_ms);
    s.addString(curve_type);
    s.addString(curve_json);
    return s;
}

// ---------------------------------------------------------------------------
// start_fade tests
// ---------------------------------------------------------------------------

/** Well-formed start_fade → Ok with all fields populated. */
static bool test_start_fade_accepts_well_formed() {
    auto s = makeStartFadeArgs("fade-001", "node-A", "127.0.0.1", 9001, "/vol",
                               0.0f, 1.0f, 86400000, 5000, "linear", "{}");

    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhiss",
        s.argv(), s.argc(), "node-A", &cmd);

    ASSERT_TRUE(rc == PR::Ok, "start_fade_ok: result is Ok");
    ASSERT_EQ_STR(cmd.motion_id.c_str(), "fade-001", "start_fade_ok: motion_id");
    ASSERT_EQ_STR(cmd.node_name.c_str(), "node-A",   "start_fade_ok: node_name");
    ASSERT_EQ_STR(cmd.osc_host.c_str(),  "127.0.0.1","start_fade_ok: osc_host");
    ASSERT_EQ_INT(cmd.osc_port, 9001,                "start_fade_ok: osc_port");
    ASSERT_EQ_STR(cmd.osc_path.c_str(),  "/vol",     "start_fade_ok: osc_path");
    ASSERT_TRUE(cmd.start_value == 0.0f,             "start_fade_ok: start_value");
    ASSERT_TRUE(cmd.end_value   == 1.0f,             "start_fade_ok: end_value");
    ASSERT_TRUE(cmd.start_mtc_ms == 86400000,        "start_fade_ok: start_mtc_ms");
    ASSERT_TRUE(cmd.duration_ms == 5000.0f,          "start_fade_ok: duration_ms");
    ASSERT_EQ_STR(cmd.curve_type.c_str(), "linear",  "start_fade_ok: curve_type");
    ASSERT_TRUE(cmd.curve_params.is_object(),         "start_fade_ok: curve_params is object");
    return true;
}

/** Wrong type tag → TypeError. */
static bool test_start_fade_wrong_type_tag() {
    auto s = makeStartFadeArgs();
    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhisX",   // 'X' instead of last 's'
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::TypeError, "start_fade_wrong_tag: TypeError");
    return true;
}

/** Too few arguments (argc mismatch) → TypeError. */
static bool test_start_fade_too_few_args() {
    auto s = makeStartFadeArgs();
    gme::signal::FadeCommand cmd;
    // Pass 10 args instead of 11
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhiss",
        s.argv(), 10, "node-A", &cmd);
    ASSERT_TRUE(rc == PR::TypeError, "start_fade_few_args: TypeError");
    return true;
}

/** node_name mismatch → NodeMismatch (with motion_id still set). */
static bool test_start_fade_node_mismatch() {
    auto s = makeStartFadeArgs("fade-X", "node-B");
    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhiss",
        s.argv(), s.argc(), "node-A", &cmd);  // daemon is node-A, msg says node-B
    ASSERT_TRUE(rc == PR::NodeMismatch, "node_mismatch: NodeMismatch");
    // motion_id should still be populated so caller can log it
    ASSERT_EQ_STR(cmd.motion_id.c_str(), "fade-X", "node_mismatch: motion_id set");
    return true;
}

/** Empty motion_id → MissingField. */
static bool test_start_fade_empty_motion_id() {
    auto s = makeStartFadeArgs("");  // empty motion_id
    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhiss",
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::MissingField, "empty_motion_id: MissingField");
    return true;
}

/** osc_port = 0 → MissingField. */
static bool test_start_fade_invalid_osc_port() {
    auto s = makeStartFadeArgs("f1", "node-A", "127.0.0.1", 0);
    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhiss",
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::MissingField, "invalid_osc_port: MissingField");
    ASSERT_EQ_STR(cmd.motion_id.c_str(), "f1", "invalid_osc_port: motion_id set");
    return true;
}

/** duration_ms = 0 → MissingField (note: 0 is valid as zero-duration).
 *  Actually per contract: duration_ms > 0 is required. */
static bool test_start_fade_zero_duration() {
    auto s = makeStartFadeArgs("f1", "node-A", "127.0.0.1", 9001, "/vol",
                               0.0f, 1.0f, 0, 0 /* duration = 0 */);
    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhiss",
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::MissingField, "zero_duration: MissingField");
    return true;
}

/** Malformed curve_params_json → TypeError. */
static bool test_start_fade_bad_curve_json() {
    auto s = makeStartFadeArgs("f1", "node-A", "127.0.0.1", 9001, "/vol",
                               0.0f, 1.0f, 0, 1000, "bezier", "NOT-JSON");
    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhiss",
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::TypeError, "bad_curve_json: TypeError");
    ASSERT_EQ_STR(cmd.motion_id.c_str(), "f1", "bad_curve_json: motion_id set");
    return true;
}

/** Valid bezier curve_params JSON → Ok with parsed curve_params. */
static bool test_start_fade_bezier_json() {
    auto s = makeStartFadeArgs("f1", "node-A", "127.0.0.1", 9001, "/vol",
                               0.0f, 1.0f, 0, 1000, "bezier",
                               "{\"p1\":[0.25,0.1],\"p2\":[0.75,0.9]}");
    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/start_fade", "sssisffhiss",
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::Ok, "bezier_json: Ok");
    ASSERT_TRUE(cmd.curve_params.contains("p1"), "bezier_json: p1 key present");
    return true;
}

// ---------------------------------------------------------------------------
// cancel_motion tests
// ---------------------------------------------------------------------------

/** Well-formed cancel_motion → Ok with motion_id and node_name. */
static bool test_cancel_motion_ok() {
    ArgStore s;
    s.addString("fade-001");
    s.addString("node-A");

    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/cancel_motion", "ss",
        s.argv(), s.argc(), "node-A", &cmd);

    ASSERT_TRUE(rc == PR::Ok, "cancel_motion_ok: Ok");
    ASSERT_TRUE(cmd.type == gme::signal::FadeCommand::Type::CANCEL_MOTION,
                "cancel_motion_ok: type");
    ASSERT_EQ_STR(cmd.motion_id.c_str(), "fade-001", "cancel_motion_ok: motion_id");
    return true;
}

/** cancel_motion with wrong type tag → TypeError. */
static bool test_cancel_motion_wrong_tag() {
    ArgStore s;
    s.addString("f1");
    s.addString("node-A");

    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/cancel_motion", "si",  // wrong: 'i' instead of 's'
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::TypeError, "cancel_motion_wrong_tag: TypeError");
    return true;
}

/** cancel_motion with node mismatch → NodeMismatch. */
static bool test_cancel_motion_node_mismatch() {
    ArgStore s;
    s.addString("f1");
    s.addString("node-B");

    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/cancel_motion", "ss",
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::NodeMismatch, "cancel_motion_node_mismatch: NodeMismatch");
    ASSERT_EQ_STR(cmd.motion_id.c_str(), "f1", "cancel_motion_node_mismatch: motion_id set");
    return true;
}

// ---------------------------------------------------------------------------
// cancel_all tests
// ---------------------------------------------------------------------------

/** Well-formed cancel_all → Ok. */
static bool test_cancel_all_ok() {
    ArgStore s;
    s.addString("node-A");

    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/cancel_all", "s",
        s.argv(), s.argc(), "node-A", &cmd);

    ASSERT_TRUE(rc == PR::Ok, "cancel_all_ok: Ok");
    ASSERT_TRUE(cmd.type == gme::signal::FadeCommand::Type::CANCEL_ALL,
                "cancel_all_ok: type");
    return true;
}

/** cancel_all with node mismatch → NodeMismatch. */
static bool test_cancel_all_node_mismatch() {
    ArgStore s;
    s.addString("node-B");

    gme::signal::FadeCommand cmd;
    auto rc = gme::signal::parseFadeOscCommand(
        "/gradient/cancel_all", "s",
        s.argv(), s.argc(), "node-A", &cmd);
    ASSERT_TRUE(rc == PR::NodeMismatch, "cancel_all_node_mismatch: NodeMismatch");
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        // start_fade
        {"start_fade_accepts_well_formed",  test_start_fade_accepts_well_formed},
        {"start_fade_wrong_type_tag",       test_start_fade_wrong_type_tag},
        {"start_fade_too_few_args",         test_start_fade_too_few_args},
        {"start_fade_node_mismatch",        test_start_fade_node_mismatch},
        {"start_fade_empty_motion_id",      test_start_fade_empty_motion_id},
        {"start_fade_invalid_osc_port",     test_start_fade_invalid_osc_port},
        {"start_fade_zero_duration",        test_start_fade_zero_duration},
        {"start_fade_bad_curve_json",       test_start_fade_bad_curve_json},
        {"start_fade_bezier_json",          test_start_fade_bezier_json},
        // cancel_motion
        {"cancel_motion_ok",                test_cancel_motion_ok},
        {"cancel_motion_wrong_tag",         test_cancel_motion_wrong_tag},
        {"cancel_motion_node_mismatch",     test_cancel_motion_node_mismatch},
        // cancel_all
        {"cancel_all_ok",                   test_cancel_all_ok},
        {"cancel_all_node_mismatch",        test_cancel_all_node_mismatch},
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
