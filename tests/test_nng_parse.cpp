/**
 * @file test_nng_parse.cpp
 * @brief TDD tests for parseFadeCommand and classifyParseOutcome.
 *
 * Group A (11 cases): parseFadeCommand JSON → FadeCommand parse pipeline.
 * Group B (14 cases): classifyParseOutcome decision table (exhaustive).
 *
 * Total: 25 cases. No sockets required — pure in-process function calls.
 */

#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>

#include "signal/FadeCommand.h"

using gme::signal::FadeCommand;
using gme::signal::ParseResult;
using gme::signal::ParseOutcomeAction;
using gme::signal::parseFadeCommand;
using gme::signal::classifyParseOutcome;

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

static nlohmann::json make_start_fade(const std::string& target = "gradientengine",
                                       const std::string& node_name = "node1") {
    return {
        {"type",   "command"},
        {"action", "apply"},
        {"target", target},
        {"data", {
            {"command",      "start_fade"},
            {"node_name",    node_name},
            {"fade_id",      "fade_001"},
            {"osc_host",     "127.0.0.1"},
            {"osc_port",     7000},
            {"osc_path",     "/volmaster"},
            {"start_value",  0.0f},
            {"end_value",    1.0f},
            {"duration_ms",  500.0f},
            {"curve_type",   "linear"},
            {"start_mtc_ms", 1000},
            {"curve_params", {{"tension", 0.5}}}
        }}
    };
}

// ---------------------------------------------------------------------------
// Group A — parseFadeCommand (11 cases)
// ---------------------------------------------------------------------------

static bool test_parse_start_fade_roundtrip() {
    nlohmann::json env = make_start_fade();
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::Ok, "start_fade result");
    CHECK(cmd.type == FadeCommand::Type::START_FADE, "start_fade type");
    CHECK(cmd.fade_id == "fade_001", "start_fade fade_id");
    CHECK(cmd.node_name == "node1", "start_fade node_name");
    CHECK(cmd.osc_host == "127.0.0.1", "start_fade osc_host");
    CHECK(cmd.osc_port == 7000, "start_fade osc_port");
    CHECK(cmd.osc_path == "/volmaster", "start_fade osc_path key");
    CHECK(cmd.start_value == 0.0f, "start_fade start_value");
    CHECK(cmd.end_value == 1.0f, "start_fade end_value");
    CHECK(cmd.duration_ms == 500.0f, "start_fade duration_ms");
    CHECK(cmd.curve_type == "linear", "start_fade curve_type");
    CHECK(cmd.start_mtc_ms == 1000L, "start_fade start_mtc_ms");
    CHECK(!cmd.curve_params.is_null(), "start_fade curve_params present");
    return true;
}

static bool test_parse_target_mismatch() {
    FadeCommand cmd;
    {
        auto env = make_start_fade("nodeengine");
        auto r = parseFadeCommand(env, "node1", cmd);
        CHECK(r == ParseResult::TargetMismatch, "TargetMismatch nodeengine");
    }
    {
        auto env = make_start_fade("controller");
        auto r = parseFadeCommand(env, "node1", cmd);
        CHECK(r == ParseResult::TargetMismatch, "TargetMismatch controller");
    }
    return true;
}

static bool test_parse_node_mismatch() {
    auto env = make_start_fade("gradientengine", "other_node");
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::NodeMismatch, "NodeMismatch wrong node_name");
    return true;
}

static bool test_parse_malformed_json() {
    nlohmann::json env = nlohmann::json::parse("", nullptr, false);
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::MalformedJson, "MalformedJson empty bytes");
    return true;
}

static bool test_parse_missing_field() {
    auto env = make_start_fade();
    env["data"].erase("fade_id");
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::MissingField, "MissingField absent fade_id");
    return true;
}

static bool test_parse_type_error() {
    auto env = make_start_fade();
    env["data"]["duration_ms"] = "not_a_number";
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::TypeError, "TypeError duration_ms as string");
    return true;
}

static bool test_parse_unknown_command() {
    auto env = make_start_fade();
    env["data"]["command"] = "blink";
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::UnknownCommand, "UnknownCommand blink");
    return true;
}

static bool test_parse_cancel_motion() {
    // New wire command "cancel_motion" with "motion_id" key
    nlohmann::json env = {
        {"type",   "command"},
        {"action", "apply"},
        {"target", "gradientengine"},
        {"data", {
            {"command",   "cancel_motion"},
            {"node_name", "node1"},
            {"motion_id", "fade_002"}
        }}
    };
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::Ok, "cancel_motion result");
    CHECK(cmd.type == FadeCommand::Type::CANCEL_MOTION, "cancel_motion type");
    CHECK(cmd.fade_id == "fade_002", "cancel_motion motion_id alias");
    return true;
}

static bool test_parse_cancel_motion_fade_id_alias() {
    // Legacy alias: "cancel_motion" with "fade_id" fallback key
    nlohmann::json env = {
        {"type",   "command"},
        {"action", "apply"},
        {"target", "gradientengine"},
        {"data", {
            {"command",   "cancel_motion"},
            {"node_name", "node1"},
            {"fade_id",   "fade_003"}
        }}
    };
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::Ok, "cancel_motion fade_id alias result");
    CHECK(cmd.type == FadeCommand::Type::CANCEL_MOTION, "cancel_motion fade_id alias type");
    CHECK(cmd.fade_id == "fade_003", "cancel_motion fade_id alias value");
    return true;
}

static bool test_parse_cancel_all() {
    nlohmann::json env = {
        {"type",   "command"},
        {"action", "apply"},
        {"target", "gradientengine"},
        {"data", {
            {"command",   "cancel_all"},
            {"node_name", "node1"}
        }}
    };
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::Ok, "cancel_all result");
    CHECK(cmd.type == FadeCommand::Type::CANCEL_ALL, "cancel_all type");
    CHECK(cmd.fade_id.empty(), "cancel_all empty fade_id");
    return true;
}

static bool test_parse_start_crossfade_roundtrip() {
    nlohmann::json env = {
        {"type",   "command"},
        {"action", "apply"},
        {"target", "gradientengine"},
        {"data", {
            {"command",             "start_crossfade"},
            {"node_name",           "node1"},
            {"fade_id",             "fade_010"},
            {"osc_host",            "127.0.0.1"},
            {"osc_port",            7000},
            {"osc_path",            "/channel/1"},
            {"start_value",         1.0f},
            {"end_value",           0.0f},
            {"duration_ms",         1000.0f},
            {"curve_type",          "sigmoid"},
            {"start_mtc_ms",        500},
            {"partner_fade_id",     "fade_011"},
            {"partner_osc_path",    "/channel/2"},
            {"partner_start_value", 0.0f},
            {"partner_end_value",   1.0f}
        }}
    };
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::Ok, "start_crossfade result");
    CHECK(cmd.type == FadeCommand::Type::START_CROSSFADE, "start_crossfade type");
    CHECK(cmd.partner_fade_id == "fade_011", "crossfade partner_fade_id");
    CHECK(cmd.partner_osc_path == "/channel/2", "crossfade partner_osc_path");
    CHECK(cmd.partner_start_value == 0.0f, "crossfade partner_start_value");
    CHECK(cmd.partner_end_value == 1.0f, "crossfade partner_end_value");
    return true;
}

static bool test_parse_forward_compat() {
    auto env = make_start_fade();
    env["data"]["unknown_field_xyz"] = "ignored";
    env["data"]["curve_params"]["newKnob"] = 42;
    FadeCommand cmd;
    auto r = parseFadeCommand(env, "node1", cmd);
    CHECK(r == ParseResult::Ok, "forward_compat result Ok");
    CHECK(cmd.curve_params.contains("tension"), "existing param preserved");
    CHECK(cmd.curve_params.contains("newKnob"), "newKnob preserved");
    CHECK(cmd.curve_params["newKnob"] == 42, "newKnob value correct");
    return true;
}

// ---------------------------------------------------------------------------
// Group B — classifyParseOutcome decision table (14 cases, exhaustive)
// ---------------------------------------------------------------------------

static bool test_classify_ok_no_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::Ok, false) == ParseOutcomeAction::Enqueue,
          "Ok/noFadeId -> Enqueue");
    return true;
}

static bool test_classify_ok_with_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::Ok, true) == ParseOutcomeAction::Enqueue,
          "Ok/hasFadeId -> Enqueue");
    return true;
}

static bool test_classify_target_mismatch_no_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::TargetMismatch, false) == ParseOutcomeAction::DropSilent,
          "TargetMismatch/noFadeId -> DropSilent");
    return true;
}

static bool test_classify_target_mismatch_with_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::TargetMismatch, true) == ParseOutcomeAction::DropSilent,
          "TargetMismatch/hasFadeId -> DropSilent");
    return true;
}

static bool test_classify_node_mismatch_no_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::NodeMismatch, false) == ParseOutcomeAction::DropSilent,
          "NodeMismatch/noFadeId -> DropSilent");
    return true;
}

static bool test_classify_node_mismatch_with_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::NodeMismatch, true) == ParseOutcomeAction::DropSilent,
          "NodeMismatch/hasFadeId -> DropSilent");
    return true;
}

static bool test_classify_unknown_command_no_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::UnknownCommand, false) == ParseOutcomeAction::LogOnly,
          "UnknownCommand/noFadeId -> LogOnly");
    return true;
}

static bool test_classify_unknown_command_with_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::UnknownCommand, true) == ParseOutcomeAction::LogOnly,
          "UnknownCommand/hasFadeId -> LogOnly");
    return true;
}

static bool test_classify_missing_field_no_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::MissingField, false) == ParseOutcomeAction::LogOnly,
          "MissingField/noFadeId -> LogOnly");
    return true;
}

static bool test_classify_missing_field_with_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::MissingField, true) == ParseOutcomeAction::LogAndStatus,
          "MissingField/hasFadeId -> LogAndStatus");
    return true;
}

static bool test_classify_type_error_no_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::TypeError, false) == ParseOutcomeAction::LogOnly,
          "TypeError/noFadeId -> LogOnly");
    return true;
}

static bool test_classify_type_error_with_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::TypeError, true) == ParseOutcomeAction::LogAndStatus,
          "TypeError/hasFadeId -> LogAndStatus");
    return true;
}

static bool test_classify_malformed_json_no_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::MalformedJson, false) == ParseOutcomeAction::LogOnly,
          "MalformedJson/noFadeId -> LogOnly");
    return true;
}

static bool test_classify_malformed_json_with_fade_id() {
    CHECK(classifyParseOutcome(ParseResult::MalformedJson, true) == ParseOutcomeAction::LogOnly,
          "MalformedJson/hasFadeId -> LogOnly");
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    struct Test { const char* name; bool(*fn)(); };
    Test tests[] = {
        // Group A — parseFadeCommand (11 cases)
        { "parse_start_fade_roundtrip",      test_parse_start_fade_roundtrip      },
        { "parse_target_mismatch",           test_parse_target_mismatch           },
        { "parse_node_mismatch",             test_parse_node_mismatch             },
        { "parse_malformed_json",            test_parse_malformed_json            },
        { "parse_missing_field",             test_parse_missing_field             },
        { "parse_type_error",                test_parse_type_error                },
        { "parse_unknown_command",           test_parse_unknown_command           },
        { "parse_cancel_motion",             test_parse_cancel_motion             },
        { "parse_cancel_motion_fade_id_alias", test_parse_cancel_motion_fade_id_alias },
        { "parse_cancel_all",                test_parse_cancel_all                },
        { "parse_start_crossfade_roundtrip", test_parse_start_crossfade_roundtrip },
        { "parse_forward_compat",            test_parse_forward_compat            },
        // Group B — classifyParseOutcome (14 cases)
        { "classify_ok_no_fade_id",                test_classify_ok_no_fade_id                },
        { "classify_ok_with_fade_id",              test_classify_ok_with_fade_id              },
        { "classify_target_mismatch_no_fade_id",   test_classify_target_mismatch_no_fade_id   },
        { "classify_target_mismatch_with_fade_id", test_classify_target_mismatch_with_fade_id },
        { "classify_node_mismatch_no_fade_id",     test_classify_node_mismatch_no_fade_id     },
        { "classify_node_mismatch_with_fade_id",   test_classify_node_mismatch_with_fade_id   },
        { "classify_unknown_command_no_fade_id",   test_classify_unknown_command_no_fade_id   },
        { "classify_unknown_command_with_fade_id", test_classify_unknown_command_with_fade_id },
        { "classify_missing_field_no_fade_id",     test_classify_missing_field_no_fade_id     },
        { "classify_missing_field_with_fade_id",   test_classify_missing_field_with_fade_id   },
        { "classify_type_error_no_fade_id",        test_classify_type_error_no_fade_id        },
        { "classify_type_error_with_fade_id",      test_classify_type_error_with_fade_id      },
        { "classify_malformed_json_no_fade_id",    test_classify_malformed_json_no_fade_id    },
        { "classify_malformed_json_with_fade_id",  test_classify_malformed_json_with_fade_id  },
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
