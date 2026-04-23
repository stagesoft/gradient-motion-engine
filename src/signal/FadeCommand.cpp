/**
 * @file FadeCommand.cpp
 * @brief Implementation of parseFadeCommand and classifyParseOutcome.
 */

#include "signal/FadeCommand.h"

namespace gme {
namespace signal {

// ---------------------------------------------------------------------------
// Anonymous-namespace helpers (T011)
// ---------------------------------------------------------------------------

namespace {

bool getString(const nlohmann::json& obj, const char* key, std::string& out) {
    if (!obj.contains(key)) return false;
    if (!obj[key].is_string()) return false;
    out = obj[key].get<std::string>();
    return true;
}

bool getInt(const nlohmann::json& obj, const char* key, int& out) {
    if (!obj.contains(key)) return false;
    if (!obj[key].is_number_integer()) return false;
    out = obj[key].get<int>();
    return true;
}

bool getFloat(const nlohmann::json& obj, const char* key, float& out) {
    if (!obj.contains(key)) return false;
    if (!obj[key].is_number()) return false;
    out = obj[key].get<float>();
    return true;
}

bool getLong(const nlohmann::json& obj, const char* key, long& out) {
    if (!obj.contains(key)) return false;
    if (!obj[key].is_number_integer()) return false;
    out = obj[key].get<long>();
    return true;
}

// ---------------------------------------------------------------------------
// Per-command parsers
// ---------------------------------------------------------------------------

ParseResult parseStartFade(const nlohmann::json& data, FadeCommand& out) {
    out.type = FadeCommand::Type::START_FADE;

    if (!getString(data, "fade_id", out.fade_id))
        return data.contains("fade_id") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getString(data, "node_name", out.node_name))
        return data.contains("node_name") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getString(data, "osc_host", out.osc_host))
        return data.contains("osc_host") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getInt(data, "osc_port", out.osc_port))
        return data.contains("osc_port") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getString(data, "osc_path", out.osc_path))
        return data.contains("osc_path") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getFloat(data, "start_value", out.start_value))
        return data.contains("start_value") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getFloat(data, "end_value", out.end_value))
        return data.contains("end_value") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getFloat(data, "duration_ms", out.duration_ms))
        return data.contains("duration_ms") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getString(data, "curve_type", out.curve_type))
        return data.contains("curve_type") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getLong(data, "start_mtc_ms", out.start_mtc_ms))
        return data.contains("start_mtc_ms") ? ParseResult::TypeError : ParseResult::MissingField;

    if (data.contains("curve_params") && data["curve_params"].is_object())
        out.curve_params = data["curve_params"];
    else
        out.curve_params = nlohmann::json::object();

    return ParseResult::Ok;
}

ParseResult parseCancelMotion(const nlohmann::json& data, FadeCommand& out) {
    out.type = FadeCommand::Type::CANCEL_MOTION;
    // Accept "motion_id" (new wire key) or "fade_id" (legacy alias)
    if (data.contains("motion_id") && data["motion_id"].is_string()) {
        out.fade_id = data["motion_id"].get<std::string>();
    } else if (!getString(data, "fade_id", out.fade_id)) {
        return data.contains("fade_id") ? ParseResult::TypeError : ParseResult::MissingField;
    }
    return ParseResult::Ok;
}

ParseResult parseCancelAll(const nlohmann::json& /*data*/, FadeCommand& out) {
    out.type = FadeCommand::Type::CANCEL_ALL;
    out.fade_id = "";
    return ParseResult::Ok;
}

ParseResult parseStartCrossfade(const nlohmann::json& data, FadeCommand& out) {
    ParseResult r = parseStartFade(data, out);
    if (r != ParseResult::Ok) return r;

    out.type = FadeCommand::Type::START_CROSSFADE;

    if (!getString(data, "partner_fade_id", out.partner_fade_id))
        return data.contains("partner_fade_id") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getString(data, "partner_osc_path", out.partner_osc_path))
        return data.contains("partner_osc_path") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getFloat(data, "partner_start_value", out.partner_start_value))
        return data.contains("partner_start_value") ? ParseResult::TypeError : ParseResult::MissingField;
    if (!getFloat(data, "partner_end_value", out.partner_end_value))
        return data.contains("partner_end_value") ? ParseResult::TypeError : ParseResult::MissingField;

    return ParseResult::Ok;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// parseFadeCommand top-level dispatch (T012)
// ---------------------------------------------------------------------------

ParseResult parseFadeCommand(const nlohmann::json& envelope,
                             const std::string& ownNodeName,
                             FadeCommand& out) {
    // (1) MalformedJson guard
    if (envelope.is_discarded() || !envelope.is_object())
        return ParseResult::MalformedJson;

    // (2) TargetMismatch — highest-traffic filter first
    if (!envelope.contains("target") || !envelope["target"].is_string() ||
        envelope["target"].get<std::string>() != "gradientengine")
        return ParseResult::TargetMismatch;

    // (3) data object presence
    if (!envelope.contains("data") || !envelope["data"].is_object())
        return ParseResult::MalformedJson;

    const nlohmann::json& data = envelope["data"];

    // (4) NodeMismatch
    std::string nodeName;
    if (!getString(data, "node_name", nodeName))
        return ParseResult::MalformedJson;
    if (nodeName != ownNodeName)
        return ParseResult::NodeMismatch;

    // (5) command dispatch
    std::string command;
    if (!getString(data, "command", command))
        return ParseResult::MalformedJson;

    if (command == "start_fade")      return parseStartFade(data, out);
    if (command == "cancel_motion")   return parseCancelMotion(data, out);
    if (command == "cancel_all")      return parseCancelAll(data, out);
    if (command == "start_crossfade") return parseStartCrossfade(data, out);

    // (6) UnknownCommand fallthrough
    return ParseResult::UnknownCommand;
}

// ---------------------------------------------------------------------------
// classifyParseOutcome decision table (T014a)
// ---------------------------------------------------------------------------

ParseOutcomeAction classifyParseOutcome(ParseResult result,
                                        bool hasFadeId) noexcept {
    switch (result) {
        case ParseResult::Ok:
            return ParseOutcomeAction::Enqueue;
        case ParseResult::TargetMismatch:
        case ParseResult::NodeMismatch:
            return ParseOutcomeAction::DropSilent;
        case ParseResult::UnknownCommand:
        case ParseResult::MalformedJson:
            return ParseOutcomeAction::LogOnly;
        case ParseResult::MissingField:
        case ParseResult::TypeError:
            return hasFadeId ? ParseOutcomeAction::LogAndStatus
                             : ParseOutcomeAction::LogOnly;
    }
    return ParseOutcomeAction::LogOnly;
}

} // namespace signal
} // namespace gme
