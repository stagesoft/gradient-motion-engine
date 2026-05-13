/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

#pragma once

#include <lo/lo.h>

#include <string>
#include <string_view>

#include "FadeCommand.h"  // ParseResult + FadeCommand struct

namespace gme {
namespace signal {

/**
 * @file parseFadeOscCommand.h
 * @brief Parse a `/gradient/\*` OSC message into a `FadeCommand` record.
 *
 * Free function — no class instantiation required. Replaces the JSON
 * parser `parseFadeCommand` (in `FadeCommand.cpp`) for the OSC transport.
 *
 * ## Supported addresses
 *
 * - `/gradient/start_fade`     → `FadeCommand{type = START_FADE, ...}`
 * - `/gradient/cancel_motion`  → `FadeCommand{type = CANCEL_MOTION, motion_id, node_name}`
 * - `/gradient/cancel_all`     → `FadeCommand{type = CANCEL_ALL, node_name}`
 *
 * ## Filter & validation order
 *
 * 1. Address → `type` discriminator.
 * 2. Type-tag must match the address's required signature exactly (TypeError).
 * 3. `node_name` compared to `this_node_name` (NodeMismatch — drop silently).
 * 4. Field-level validation:
 *    - `start_fade`: motion_id, osc_host, osc_path non-empty; osc_port > 0;
 *      duration_ms > 0 (MissingField). curve_params_json parsed as JSON
 *      object (TypeError on failure).
 *    - `cancel_motion`: motion_id non-empty (MissingField).
 *    - `cancel_all`: no additional checks.
 *
 * On `MissingField`/`TypeError` for start_fade/cancel_motion that did
 * provide a parseable motion_id, out_cmd->motion_id and ->type are set
 * so the caller can log the rejection with motion_id named.
 *
 * @param path             OSC address string. Required.
 * @param types            OSC type-tag string (without leading `,`). Required.
 * @param argv             Array of lo_arg* pointers. Required.
 * @param argc             Number of arguments.
 * @param this_node_name   Daemon's configured node name.
 * @param out_cmd          Output. Must NOT be nullptr.
 *
 * @return ParseResult::Ok on success, otherwise the specific failure mode.
 *
 * @par Example (inside an OscServer callback):
 * @code
 *   gme::signal::FadeCommand cmd;
 *   auto rc = gme::signal::parseFadeOscCommand(
 *       path, types, argv, argc, ctx->node_name, &cmd);
 *   if (rc == gme::signal::ParseResult::Ok)
 *       ctx->out_queue->push(std::move(cmd));
 * @endcode
 */
ParseResult parseFadeOscCommand(const char* path,
                                const char* types,
                                lo_arg** argv,
                                int argc,
                                std::string_view this_node_name,
                                FadeCommand* out_cmd);

}  // namespace signal
}  // namespace gme
