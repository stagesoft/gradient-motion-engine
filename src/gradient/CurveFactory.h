/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

#include "Curve.h"

namespace gme {
namespace gradient {

/**
 * @brief Static factory for creating named curve instances.
 *
 * CurveFactory is the single entry point for constructing `gme::gradient`
 * curves from a string type name and an optional JSON parameter object.
 * All returned curves are wrapped in a `ResampledCurve` (256-sample LUT)
 * for constant-time evaluation during playback.
 */
class CurveFactory {
public:
    /**
     * @brief Create a curve by type name with optional JSON parameters.
     *
     * @details
     * Constructs the curve type identified by \p type, applies parameters
     * from \p params (using documented defaults for any missing keys), wraps
     * the result in a `ResampledCurve`, and returns it inside a
     * `std::optional`.
     *
     * **Supported type names and their parameter keys**:
     * | type      | key          | C++ type | default |
     * |-----------|--------------|----------|---------|
     * | "linear"  | —            | —        | —       |
     * | "sigmoid" | "steepness"  | double   | 8.0     |
     * | "sigmoid" | "midpoint"   | double   | 0.5     |
     * | "bezier"  | "cx1"        | double   | 0.25    |
     * | "bezier"  | "cy1"        | double   | 0.1     |
     * | "bezier"  | "cx2"        | double   | 0.75    |
     * | "bezier"  | "cy2"        | double   | 0.9     |
     * | "ease_in" | "exponent"   | double   | 2.0     |
     * | "ease_out"| "exponent"   | double   | 2.0     |
     * | "scurve"  | —            | —        | —       |
     *
     * Any other string for \p type returns `std::nullopt`.
     *
     * @param type   String identifying the curve shape. Case-sensitive.
     *               Supported values: "linear", "sigmoid", "bezier",
     *               "ease_in", "ease_out", "scurve".
     * @param params JSON object containing curve-specific parameters.
     *               Missing keys silently use the documented defaults.
     *               Malformed values (wrong type) also use defaults.
     *               Defaults to an empty object `{}`.
     *
     * @return On success: `std::optional` containing an owned
     *         `ResampledCurve`-wrapped `Curve` instance.
     *         On unknown \p type: `std::nullopt`.
     *
     * @note **Why `std::optional` rather than a silent fallback?**
     *       Returning `std::nullopt` for an unknown type makes the
     *       error *observable* at the call site. The caller (typically
     *       the engine's `FadeRegistry`) can then apply the policy that
     *       is appropriate for its context — log a warning, alert the
     *       operator, substitute a default, or abort the cue — rather
     *       than the factory silently substituting a `LinearCurve` that
     *       may misrepresent the cue designer's intended curve shape.
     *       This satisfies the constitution's requirement for explicit
     *       error signalling without crossing the "no exceptions across
     *       library boundaries" constraint.
     *
     * @par Error handling
     * - This function **never throws**.
     * - Unknown \p type → emits a single-line warning to `stderr` and
     *   returns `std::nullopt`. The caller is responsible for any fallback.
     * - Malformed or missing \p params keys → defaults are applied silently;
     *   a valid curve is always returned for known types.
     *
     * @code
     * using namespace gme::gradient;
     *
     * // --- Successful creation ---
     * nlohmann::json p;
     * p["steepness"] = 12.0;
     * auto result = CurveFactory::createCurve("sigmoid", p);
     * if (result.has_value()) {
     *     double v = (*result)->evaluate(0.5);
     * }
     *
     * // --- Recommended fallback pattern for unknown types ---
     * auto r2 = CurveFactory::createCurve("unknown_type", {});
     * auto curve = r2
     *     ? std::move(*r2)
     *     : std::make_unique<ResampledCurve>(LinearCurve{});
     * // curve is always non-null; caller decided the fallback policy.
     * @endcode
     */
    static std::optional<std::unique_ptr<Curve>>
    createCurve(const std::string& type,
                const nlohmann::json& params = nlohmann::json::object());

private:
    CurveFactory() = delete; ///< Static-only class; not instantiable.
};

} // namespace gradient
} // namespace gme
