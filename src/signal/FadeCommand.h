/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file FadeCommand.h
 * @brief Command record dispatched from the Controller to the fade engine.
 *
 * `FadeCommand` is the in-memory representation of a single NNG fade
 * instruction after JSON parsing and target/node filtering. It carries
 * every field required to execute a fade (OSC target, value range,
 * curve configuration, timing origin) plus the four supported command
 * variants (`START_FADE`, `CANCEL_FADE`, `CANCEL_ALL`,
 * `START_CROSSFADE`). The struct is a plain aggregate — no behaviour,
 * no invariants beyond its fields. It is the sole payload type handed
 * from the NNG receive thread to the MTC tick thread via
 * `gme::signal::LockFreeQueue<FadeCommand, 64>`.
 *
 * ## Required-field rules
 *
 * The free function `parseFadeCommand()` enforces the required-field
 * matrix below. Missing required fields, wrong-typed fields, or
 * malformed JSON cause the message to be rejected (ParseResult values
 * other than `Ok`).
 *
 * | Command          | Required in `data`                                                 |
 * |------------------|--------------------------------------------------------------------|
 * | start_fade       | fade_id, node_name, osc_host, osc_port, osc_path, start_value,     |
 * |                  | end_value, duration_ms, curve_type, start_mtc_ms                   |
 * | cancel_motion    | motion_id (alias: fade_id), node_name                              |
 * | cancel_all       | node_name                                                          |
 * | start_crossfade  | start_fade fields + partner_fade_id, partner_osc_path,             |
 * |                  | partner_start_value, partner_end_value                             |
 *
 * `curve_params` is optional. When absent, `FadeCommand::curve_params`
 * is stored as an empty JSON object (`nlohmann::json::object()`).
 * Unknown top-level keys in `data` and unknown keys inside
 * `curve_params` are silently ignored (FR-014, forward-compatibility).
 *
 * ## Wire/struct key parity
 *
 * Wire JSON key names match the C++ struct field names verbatim —
 * notably `osc_path` (OSC destination path, liblo terminology) and
 * `partner_osc_path` for the crossfade B-side. The Python-side emitter
 * (Phase 6) MUST use these same key names — no `osc_address` rename.
 *
 * @par Example usage:
 * @code
 *   nlohmann::json env = nlohmann::json::parse(bytes, nullptr, false);
 *   if (env.is_discarded()) { return; } // malformed JSON
 *
 *   FadeCommand cmd;
 *   switch (gme::signal::parseFadeCommand(env, myNodeName, cmd)) {
 *       case gme::signal::ParseResult::Ok:
 *           queue.push(std::move(cmd));
 *           break;
 *       case gme::signal::ParseResult::TargetMismatch:
 *       case gme::signal::ParseResult::NodeMismatch:
 *           // Drop silently — message was not for us.
 *           break;
 *       case gme::signal::ParseResult::MissingField:
 *       case gme::signal::ParseResult::TypeError:
 *           if (!cmd.fade_id.empty())
 *               client.sendStatus(StatusKind::MotionError, cmd.fade_id, "parse_error");
 *           break;
 *       default:
 *           // Log only.
 *           break;
 *   }
 * @endcode
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace gme {
namespace signal {

/**
 * @brief Command record produced from a parsed NNG fade message.
 *
 * Plain aggregate struct. Zero invariants beyond the field defaults.
 * Default construction produces a well-formed "empty" command that
 * the parser will overwrite before enqueuing.
 */
struct FadeCommand {
    /**
     * @brief The four command variants carried on the NNG bus.
     */
    enum class Type {
        START_FADE,      ///< Start a single fade on one OSC endpoint.
        CANCEL_MOTION,   ///< Cancel an active motion by fade_id (wire key: "cancel_motion").
        CANCEL_ALL,      ///< Cancel every active motion (project unload/stop).
        START_CROSSFADE  ///< Start two linked fades sharing timing + curve.
    };

    Type type = Type::START_FADE;

    std::string fade_id;         ///< Controller-assigned unique id. Empty for CANCEL_ALL.
    std::string node_name;       ///< Target node name. Required for all commands.

    // OSC endpoint (primary fade). Unused for CANCEL_FADE / CANCEL_ALL.
    std::string osc_host;        ///< Usually "127.0.0.1".
    int         osc_port = 0;    ///< AudioPlayer port or 7000 (VideoComposer).
    std::string osc_path;        ///< e.g. "/volmaster" or "/videocomposer/layer/3/opacity".

    float start_value  = 0.0f;   ///< Start gain (0.0–1.0).
    float end_value    = 0.0f;   ///< End gain (0.0–1.0).
    float duration_ms  = 0.0f;   ///< Fade duration in ms.

    std::string       curve_type;     ///< e.g. "linear", "sigmoid", "bezier".
    nlohmann::json    curve_params;   ///< Pass-through; interpreted by CurveFactory.

    long start_mtc_ms = -1;      ///< Absolute MTC start time. -1 = "start at current MTC".

    // Crossfade B-side fields. Empty / 0.0 except when type == START_CROSSFADE.
    std::string partner_fade_id;
    std::string partner_osc_path;
    float       partner_start_value = 0.0f;
    float       partner_end_value   = 0.0f;
};

/**
 * @brief Outcome categories returned by `parseFadeCommand`.
 *
 * Callers distinguish silent-drop outcomes (`TargetMismatch`,
 * `NodeMismatch`) from loggable errors (`MissingField`, `TypeError`,
 * `UnknownCommand`, `MalformedJson`). On `MissingField` / `TypeError`
 * the parser populates `out.fade_id` if it was parseable, so the
 * caller can attribute a `fade_error` status message.
 */
enum class ParseResult {
    Ok,                 ///< `out` is populated; caller should enqueue.
    TargetMismatch,     ///< Drop silently (target != "gradientengine").
    NodeMismatch,       ///< Drop silently (data.node_name != own node).
    UnknownCommand,     ///< Log warning; `data.command` is not recognised.
    MissingField,       ///< Required field absent; log + optional fade_error.
    TypeError,          ///< Required field wrong type; log + optional fade_error.
    MalformedJson       ///< Envelope is not a JSON object.
};

/**
 * @brief Parse a NodeOperation JSON envelope into a FadeCommand.
 *
 * Pure function — no I/O, no global state, safe to call from any
 * thread. Does all validation required by the spec:
 *
 *  1. Short-circuit on `envelope["target"] != "gradientengine"` →
 *     `TargetMismatch` (highest-traffic filter first). `gradientengine`
 *     is the uniform inbound target for this daemon; fade-specific
 *     dispatch happens on `data.command`, not on `target`.
 *  2. Short-circuit on `envelope["data"]["node_name"] != ownNodeName` →
 *     `NodeMismatch`.
 *  3. Dispatch on `envelope["data"]["command"]`; populate `out` from
 *     the command-specific required fields (see class comment).
 *  4. Forward-compat: unknown top-level keys in `data` and unknown
 *     keys inside `curve_params` are silently ignored.
 *
 * @param envelope     Parsed JSON envelope from `nng_recv`. If
 *                     `envelope.is_discarded()` or not an object,
 *                     returns `MalformedJson`.
 * @param ownNodeName  The daemon's own node name (from config).
 * @param out          [out] Populated on `Ok`. On `MissingField` /
 *                     `TypeError`, `out.fade_id` is populated if
 *                     parseable (empty string otherwise). Undefined on
 *                     every other outcome.
 *
 * @return One of the `ParseResult` values.
 *
 * @throws None. nlohmann/json is used with `allow_exceptions=false`
 *         equivalent accessors (`.contains()`, `.is_*()`) so no
 *         exception can escape this function.
 *
 * @par Example:
 * @code
 *   FadeCommand cmd;
 *   auto r = parseFadeCommand(env, "nodeA", cmd);
 *   if (r == ParseResult::Ok) queue.push(std::move(cmd));
 * @endcode
 */
ParseResult parseFadeCommand(const nlohmann::json& envelope,
                             const std::string& ownNodeName,
                             FadeCommand& out);

/**
 * @brief Action to take in response to a `ParseResult`.
 *
 * Separates the "what do we do next?" decision from the "what did
 * parsing produce?" result, so the dispatch table can be unit-tested
 * without a real socket or a live logger (see `classifyParseOutcome`).
 *
 * Authoritative mapping (FR-012, FR-014; tested in `test_nng_parse.cpp`):
 *
 * | ParseResult      | hasFadeId=false | hasFadeId=true   |
 * |------------------|-----------------|------------------|
 * | Ok               | Enqueue         | Enqueue          |
 * | TargetMismatch   | DropSilent      | DropSilent       |
 * | NodeMismatch     | DropSilent      | DropSilent       |
 * | UnknownCommand   | LogOnly         | LogOnly          |
 * | MissingField     | LogOnly         | LogAndStatus     |
 * | TypeError        | LogOnly         | LogAndStatus     |
 * | MalformedJson    | LogOnly         | LogOnly          |
 */
enum class ParseOutcomeAction {
    Enqueue,        ///< Push the parsed `FadeCommand` into the queue.
    DropSilent,     ///< Not for us; do not log, do not emit status.
    LogOnly,        ///< Emit `GME_LOG_WARNING`; no `fade_error` (no fade_id context).
    LogAndStatus    ///< Emit `GME_LOG_WARNING` AND `sendStatus(MotionError, fade_id, "parse_error")`.
};

/**
 * @brief Classify a `ParseResult` into a concrete dispatch action.
 *
 * Pure function — no I/O, no logging, no state. Intended to be called
 * by `NngBusClient::recvLoop` immediately after `parseFadeCommand`:
 *
 * @code
 *   FadeCommand cmd;
 *   ParseResult r = parseFadeCommand(env, nodeName_, cmd);
 *   switch (classifyParseOutcome(r, !cmd.fade_id.empty())) {
 *       case ParseOutcomeAction::Enqueue:
 *           queue_.push(std::move(cmd));
 *           break;
 *       case ParseOutcomeAction::DropSilent:
 *           break;
 *       case ParseOutcomeAction::LogOnly:
 *           GME_LOG_WARNING("parse rejected: " + describeParseResult(r));
 *           break;
 *       case ParseOutcomeAction::LogAndStatus:
 *           GME_LOG_WARNING("parse rejected: " + describeParseResult(r));
 *           sendStatus(StatusKind::MotionError, cmd.fade_id, "parse_error");
 *           break;
 *   }
 * @endcode
 *
 * @param result     The outcome returned by `parseFadeCommand`.
 * @param hasFadeId  `true` if `FadeCommand::fade_id` is non-empty
 *                   (i.e. a parseable `fade_id` is available for
 *                   status attribution). The caller computes this
 *                   from the `out` parameter after parsing.
 *
 * @return The corresponding `ParseOutcomeAction`. Never throws.
 *
 * @throws None.
 */
ParseOutcomeAction classifyParseOutcome(ParseResult result,
                                        bool hasFadeId) noexcept;

} // namespace signal
} // namespace gme
