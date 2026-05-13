/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 *
 * Contract header for src/signal/parseFadeOscCommand (Phase H — feature 007).
 * Authoritative shape for the implementation. The final installed file lives
 * at src/signal/parseFadeOscCommand.h with the same public surface.
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
 * @brief Parse a `/gradient/*` OSC message into a `FadeCommand` record.
 *
 * Free function — no class instantiation required. Replaces the JSON
 * parser `parseFadeCommand` (in `FadeCommand.h`) for the OSC transport.
 * Returns the same `ParseResult` enum so existing dispatch code in
 * `GradientEngine` reads identically against either parser during the
 * transition window. After Phase H lands and the JSON parser is removed,
 * this is the sole parser path.
 *
 * ## Supported addresses
 *
 * See [`specs/007-osc-input-transport/contracts/gradient_osc.md`](../specs/007-osc-input-transport/contracts/gradient_osc.md)
 * for the canonical wire contract. In summary:
 *
 * - `/gradient/start_fade`     → `FadeCommand{type = START_FADE, ...}`
 * - `/gradient/cancel_motion`  → `FadeCommand{type = CANCEL_FADE, motion_id, node_name}`
 * - `/gradient/cancel_all`     → `FadeCommand{type = CANCEL_ALL, node_name}`
 *
 * Any other address returns `ParseResult::MissingField` (treated as
 * "address not recognized"; the caller logs and drops). The function does
 * not consume the catch-all liblo method — the daemon's `OscServer`
 * registers explicit methods per address and only calls this function on
 * matches.
 *
 * ## Filter & validation order
 *
 * 1. Address → `type` discriminator.
 * 2. Type-tag must match the address's required signature exactly.
 *    Mismatch → `TypeError`.
 * 3. Argument count derived from type tag must match. Shortfall →
 *    `MissingField`.
 * 4. `node_name` argument compared to `this_node_name`.
 *    Mismatch → `NodeMismatch` (drop silently; daemon logs at debug).
 * 5. Field-level validation per address:
 *    - `start_fade`: `motion_id`, `osc_host`, `osc_path` non-empty;
 *      `osc_port > 0`; `duration_ms > 0`. Failure → `MissingField`.
 *      `curve_params_json` parsed via `nlohmann::json::parse`; failure
 *      → `TypeError`. `curve_params_json` MAY be `"{}"`; the resulting
 *      `FadeCommand.curve_params` is an empty JSON object in that case.
 *    - `cancel_motion`: `motion_id` non-empty. Failure → `MissingField`.
 *    - `cancel_all`: no additional checks.
 *
 * On `ParseResult::Ok`, `out_cmd` is fully populated.
 * On `MissingField` or `TypeError` *for a `start_fade`/`cancel_motion`
 * message that did provide a parseable `motion_id`*, `out_cmd.motion_id`
 * is set to that value (and `out_cmd.type` to the matching command). All
 * other fields are unspecified. This lets the caller log the rejection
 * with the offending `motion_id` named.
 *
 * On any other rejection, `out_cmd` is left untouched.
 *
 * ## Real-time safety
 *
 * Called on the OSC server (network) thread. May allocate via
 * `nlohmann::json::parse` for `curve_params_json`. Does NOT run on the
 * tick thread — Constitution Principle IV does not constrain this
 * function's allocation behavior.
 *
 * @param path             OSC address as a null-terminated string
 *                         (`message[0]` in liblo terminology). Required.
 * @param types            OSC type-tag string (without the leading `,`),
 *                         as supplied by `liblo`'s method callback. Required.
 * @param argv             Array of `lo_arg*` pointers, one per type-tag
 *                         character. Required, exact length = `argc`.
 * @param argc             Number of arguments (length of `types` and
 *                         `argv`).
 * @param this_node_name   The daemon's configured node name. Used for the
 *                         filter at step 4 above. Non-empty.
 * @param out_cmd          Output. Populated on `Ok` (and partially populated
 *                         on `MissingField`/`TypeError` per the rule above).
 *                         Must NOT be `nullptr`.
 *
 * @return `ParseResult::Ok` on success, otherwise the specific failure mode.
 *
 * @par Example usage (inside an `OscServer` per-address callback):
 * @code
 *   int onStartFade(const char* path, const char* types,
 *                   lo_arg** argv, int argc, lo_message, void* userdata) {
 *       auto* ctx = static_cast<OscServer::Impl*>(userdata);
 *       gme::signal::FadeCommand cmd;
 *       auto rc = gme::signal::parseFadeOscCommand(
 *           path, types, argv, argc, ctx->node_name, &cmd);
 *       switch (rc) {
 *           case gme::signal::ParseResult::Ok:
 *               ctx->out_queue->push(std::move(cmd));
 *               break;
 *           case gme::signal::ParseResult::NodeMismatch:
 *               // drop silently
 *               break;
 *           case gme::signal::ParseResult::MissingField:
 *           case gme::signal::ParseResult::TypeError:
 *               ctx->logger->warn("OSC parse failed: motion_id={}", cmd.motion_id);
 *               break;
 *           default:
 *               break;
 *       }
 *       return 0;  // liblo: 0 means "handled, do not try further methods"
 *   }
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
