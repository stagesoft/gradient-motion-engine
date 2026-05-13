/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file parseFadeOscCommand.cpp
 * @brief Full implementation of parseFadeOscCommand.
 *
 * Implements all validation and field-population rules from the wire contract
 * in specs/007-osc-input-transport/contracts/gradient_osc.md:
 *  - Type-tag exact-match check (TypeError on mismatch or argc != expected).
 *  - node_name filter (NodeMismatch, silent drop).
 *  - Per-address field extraction and validation (MissingField, TypeError).
 *  - curve_params_json parsed as nlohmann::json object.
 *
 * Partial-population rule: when MissingField or TypeError is returned for
 * start_fade / cancel_motion, out_cmd->motion_id is set if it was parseable,
 * so the caller can log the rejection with the offending motion_id named.
 */

#include "signal/parseFadeOscCommand.h"

#include <cstring>
#include <nlohmann/json.hpp>

namespace gme {
namespace signal {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Extract string from lo_arg* (the arg pointer IS the char* for string args)
static inline const char* argString(lo_arg** argv, int i) {
    return reinterpret_cast<const char*>(argv[i]);
}

static inline int32_t argInt32(lo_arg** argv, int i) {
    return argv[i]->i;
}

static inline float argFloat(lo_arg** argv, int i) {
    return argv[i]->f;
}

static inline int64_t argInt64(lo_arg** argv, int i) {
    return argv[i]->h;
}

// ---------------------------------------------------------------------------
// /gradient/start_fade parser
// ---------------------------------------------------------------------------

ParseResult parseStartFade(const char* types, lo_arg** argv, int argc,
                            std::string_view this_node_name,
                            FadeCommand* out) {
    static constexpr const char* kExpectedTypes = "sssisffhiss";
    static constexpr int         kExpectedArgc  = 11;

    // 1. Type-tag + argc check
    if (argc != kExpectedArgc || std::strcmp(types, kExpectedTypes) != 0) {
        return ParseResult::TypeError;
    }

    out->type = FadeCommand::Type::START_FADE;

    // 2. Extract motion_id early for partial-population on later failures
    out->motion_id = argString(argv, 0);

    // 3. node_name filter (NodeMismatch — drop silently)
    const char* node_name = argString(argv, 1);
    if (std::string_view(node_name) != this_node_name) {
        return ParseResult::NodeMismatch;
    }
    out->node_name = node_name;

    // 4. Field validation
    if (out->motion_id.empty()) return ParseResult::MissingField;

    out->osc_host = argString(argv, 2);
    if (out->osc_host.empty()) return ParseResult::MissingField;

    out->osc_port = argInt32(argv, 3);
    if (out->osc_port <= 0) return ParseResult::MissingField;

    out->osc_path = argString(argv, 4);
    if (out->osc_path.empty()) return ParseResult::MissingField;

    out->start_value  = argFloat(argv, 5);
    out->end_value    = argFloat(argv, 6);
    out->start_mtc_ms = static_cast<long>(argInt64(argv, 7));

    int32_t dur = argInt32(argv, 8);
    if (dur <= 0) return ParseResult::MissingField;
    out->duration_ms = static_cast<float>(dur);

    out->curve_type = argString(argv, 9);

    // 5. Parse curve_params_json
    const char* json_str = argString(argv, 10);
    try {
        auto parsed = nlohmann::json::parse(json_str);
        if (!parsed.is_object()) {
            return ParseResult::TypeError;
        }
        out->curve_params = std::move(parsed);
    } catch (const nlohmann::json::parse_error&) {
        return ParseResult::TypeError;
    }

    return ParseResult::Ok;
}

// ---------------------------------------------------------------------------
// /gradient/cancel_motion parser
// ---------------------------------------------------------------------------

ParseResult parseCancelMotion(const char* types, lo_arg** argv, int argc,
                               std::string_view this_node_name,
                               FadeCommand* out) {
    if (argc != 2 || std::strcmp(types, "ss") != 0) {
        return ParseResult::TypeError;
    }

    out->type      = FadeCommand::Type::CANCEL_MOTION;
    out->motion_id = argString(argv, 0);

    const char* node_name = argString(argv, 1);
    if (std::string_view(node_name) != this_node_name) {
        return ParseResult::NodeMismatch;
    }
    out->node_name = node_name;

    if (out->motion_id.empty()) return ParseResult::MissingField;

    return ParseResult::Ok;
}

// ---------------------------------------------------------------------------
// /gradient/cancel_all parser
// ---------------------------------------------------------------------------

ParseResult parseCancelAll(const char* types, lo_arg** argv, int argc,
                            std::string_view this_node_name,
                            FadeCommand* out) {
    if (argc != 1 || std::strcmp(types, "s") != 0) {
        return ParseResult::TypeError;
    }

    out->type = FadeCommand::Type::CANCEL_ALL;

    const char* node_name = argString(argv, 0);
    if (std::string_view(node_name) != this_node_name) {
        return ParseResult::NodeMismatch;
    }
    out->node_name = node_name;

    return ParseResult::Ok;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

ParseResult parseFadeOscCommand(const char* path,
                                const char* types,
                                lo_arg** argv,
                                int argc,
                                std::string_view this_node_name,
                                FadeCommand* out_cmd) {
    if (!path || !out_cmd) return ParseResult::MissingField;

    if (std::strcmp(path, "/gradient/start_fade") == 0)
        return parseStartFade(types, argv, argc, this_node_name, out_cmd);

    if (std::strcmp(path, "/gradient/cancel_motion") == 0)
        return parseCancelMotion(types, argv, argc, this_node_name, out_cmd);

    if (std::strcmp(path, "/gradient/cancel_all") == 0)
        return parseCancelAll(types, argv, argc, this_node_name, out_cmd);

    return ParseResult::UnknownCommand;
}

} // namespace signal
} // namespace gme
