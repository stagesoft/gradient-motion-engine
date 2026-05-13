/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file FadeCommand.h
 * @brief Command record dispatched to the fade engine.
 *
 * `FadeCommand` is the in-memory representation of a single OSC fade
 * instruction after parsing and node filtering. It carries every field
 * required to execute a fade (OSC target, value range, curve configuration,
 * timing origin) plus the four supported command variants (`START_FADE`,
 * `CANCEL_MOTION`, `CANCEL_ALL`, `START_CROSSFADE`). The struct is a plain
 * aggregate — no behaviour, no invariants beyond its fields. It is the sole
 * payload type handed from the OSC server thread to the MTC tick thread via
 * `gme::signal::LockFreeQueue<FadeCommand, 64>`.
 *
 * ## Required-field rules
 *
 * The free function `parseFadeOscCommand()` enforces the required-field
 * matrix below. Missing required fields, wrong-typed fields, or malformed
 * JSON cause the message to be rejected (ParseResult values other than `Ok`).
 *
 * | Command          | Required                                                           |
 * |------------------|--------------------------------------------------------------------|
 * | start_fade       | motion_id, node_name, osc_host, osc_port, osc_path, start_value,  |
 * |                  | end_value, duration_ms, curve_type, start_mtc_ms                  |
 * | cancel_motion    | motion_id, node_name                                               |
 * | cancel_all       | node_name                                                          |
 * | start_crossfade  | start_fade fields + partner_motion_id, partner_osc_path,          |
 * |                  | partner_start_value, partner_end_value                            |
 *
 * `curve_params` is optional. When absent, `FadeCommand::curve_params`
 * is stored as an empty JSON object (`nlohmann::json::object()`).
 *
 * @par Example usage:
 * @code
 *   FadeCommand cmd;
 *   auto r = parseFadeOscCommand(path, types, argv, argc, nodeName, &cmd);
 *   if (r == gme::signal::ParseResult::Ok)
 *       queue.push(std::move(cmd));
 * @endcode
 */

#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace gme {
namespace signal {

/**
 * @brief Command record produced from a parsed OSC fade message.
 *
 * Plain aggregate struct. Zero invariants beyond the field defaults.
 * Default construction produces a well-formed "empty" command that
 * the parser will overwrite before enqueuing.
 */
struct FadeCommand {
    /**
     * @brief The four command variants carried on the OSC transport.
     */
    enum class Type {
        START_FADE,      ///< Start a single fade on one OSC endpoint.
        CANCEL_MOTION,   ///< Cancel an active motion by motion_id.
        CANCEL_ALL,      ///< Cancel every active motion (project unload/stop).
        START_CROSSFADE  ///< Start two linked fades sharing timing + curve (deferred).
    };

    Type type = Type::START_FADE;

    std::string motion_id;         ///< Caller-assigned unique id. Empty for CANCEL_ALL.
    std::string node_name;         ///< Target node name. Required for all commands.

    // OSC endpoint (primary fade). Unused for CANCEL_MOTION / CANCEL_ALL.
    std::string osc_host;          ///< Usually "127.0.0.1".
    int         osc_port = 0;      ///< AudioPlayer port or 7000 (VideoComposer).
    std::string osc_path;          ///< e.g. "/volmaster" or "/videocomposer/layer/3/opacity".

    float start_value  = 0.0f;     ///< Start gain (0.0–1.0).
    float end_value    = 0.0f;     ///< End gain (0.0–1.0).
    float duration_ms  = 0.0f;     ///< Fade duration in ms.

    std::string       curve_type;  ///< e.g. "linear", "sigmoid", "bezier".
    nlohmann::json    curve_params;///< Pass-through; interpreted by CurveFactory.

    long start_mtc_ms = -1;        ///< Absolute MTC start time. -1 = "start at current MTC".

    // Crossfade B-side fields. Empty / 0.0 except when type == START_CROSSFADE.
    std::string partner_motion_id;
    std::string partner_osc_path;
    float       partner_start_value = 0.0f;
    float       partner_end_value   = 0.0f;
};

/**
 * @brief Outcome categories returned by the OSC parser.
 *
 * Callers distinguish silent-drop outcomes (`NodeMismatch`) from loggable
 * errors (`MissingField`, `TypeError`). On `MissingField` / `TypeError`
 * the parser populates `out.motion_id` if it was parseable, so the caller
 * can log the rejection with the offending motion_id named.
 */
enum class ParseResult {
    Ok,                 ///< `out` is populated; caller should enqueue.
    TargetMismatch,     ///< Drop silently (legacy JSON field; OSC parser never returns this).
    NodeMismatch,       ///< Drop silently (node_name does not match).
    UnknownCommand,     ///< Log warning; address not recognised.
    MissingField,       ///< Required field absent; log warning.
    TypeError,          ///< Required field wrong type or unparseable; log warning.
    MalformedJson       ///< Envelope not a JSON object (legacy JSON path; OSC parser never returns this).
};

/**
 * @brief Discriminates between a successful motion completion and an error event.
 *
 * Used by `MotionRegistry` to classify lifecycle events for journal logging.
 */
enum class StatusKind {
    MotionComplete, ///< Motion reached t = 1.0 and was removed from the registry.
    MotionError     ///< Motion was removed due to an error (see reason field).
};

} // namespace signal
} // namespace gme
