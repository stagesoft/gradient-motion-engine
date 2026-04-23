/**
 * @file FadeRegistry.cpp
 * @brief Implementation of FadeRegistry — fade registration, tick evaluation,
 *        cancel, and status emission.
 */

#include "engine/FadeRegistry.h"
#include "gradient/CurveFactory.h"
#include "osc/OscSender.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gme {
namespace engine {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

FadeRegistry::FadeRegistry(
    const gme::time::MtcTickSource& mtcSource,
    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64>& statusQueue,
    std::function<void(gme::signal::StatusKind,
                       const std::string&,
                       const std::string&)> statusDirect,
    OscSendFn oscSend)
    : mtcSource_(mtcSource)
    , statusQueue_(statusQueue)
    , statusDirect_(std::move(statusDirect))
    , oscSend_(oscSend ? std::move(oscSend) : OscSendFn(gme::osc::sendFloat))
{}

FadeRegistry::~FadeRegistry() {
    // Free all lo_address handles before the map is destroyed
    for (auto& kv : fades_) {
        if (kv.second && kv.second->osc_target) {
            lo_address_free(kv.second->osc_target);
            kv.second->osc_target = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string FadeRegistry::oscKey(const std::string& host, int port,
                                  const std::string& path) {
    return host + ":" + std::to_string(port) + ":" + path;
}

void FadeRegistry::removeFade(const std::string& fade_id) {
    auto it = fades_.find(fade_id);
    if (it == fades_.end()) return;

    auto& fade = it->second;
    if (fade->osc_target) {
        lo_address_free(fade->osc_target);
        fade->osc_target = nullptr;
    }

    const std::string key = oscKey(fade->osc_host, fade->osc_port, fade->osc_path);
    auto oit = osc_index_.find(key);
    // Only erase the osc_index_ entry if it still points to this fade_id
    if (oit != osc_index_.end() && oit->second == fade_id) {
        osc_index_.erase(oit);
    }

    fades_.erase(it);
}

void FadeRegistry::pushStatusFromTick(gme::signal::StatusKind kind,
                                       const std::string& fade_id,
                                       const std::string& reason) {
    gme::signal::StatusEmitRequest req;
    req.kind    = kind;
    req.fade_id = fade_id;
    req.reason  = reason;
    if (!statusQueue_.push(std::move(req))) {
        std::fprintf(stderr, "WARNING FadeRegistry: status queue overflow, "
                     "oldest dropped (fade_id=%s)\n", fade_id.c_str());
    }
}

void FadeRegistry::emitStatusDirect(gme::signal::StatusKind kind,
                                     const std::string& fade_id,
                                     const std::string& reason) {
    if (statusDirect_) {
        statusDirect_(kind, fade_id, reason);
    }
}

// ---------------------------------------------------------------------------
// apply — command dispatch
// ---------------------------------------------------------------------------

void FadeRegistry::apply(gme::signal::FadeCommand& cmd) {
    using Type = gme::signal::FadeCommand::Type;
    switch (cmd.type) {
        case Type::START_FADE:
            addFade(cmd);
            break;
        case Type::CANCEL_FADE:
            cancelFade(cmd.fade_id, /*snap_to_end=*/false);
            break;
        case Type::CANCEL_ALL:
            cancelAll();
            break;
        case Type::START_CROSSFADE:
            // Phase 4: log and drop. Full semantics in Phase 7.
            std::fprintf(stderr, "INFO FadeRegistry: START_CROSSFADE dropped "
                         "(deferred to Phase 7, fade_id=%s)\n",
                         cmd.fade_id.c_str());
            break;
    }
}

// ---------------------------------------------------------------------------
// addFade (FR-015, FR-016, FR-014)
// ---------------------------------------------------------------------------

void FadeRegistry::addFade(const gme::signal::FadeCommand& cmd) {
    // --- Step 1: Build curve (FR-015: on tick thread) ---
    // Guard against null curve_params (FadeCommand default is null json, not {})
    const nlohmann::json& params = cmd.curve_params.is_null()
                                   ? nlohmann::json::object()
                                   : cmd.curve_params;
    auto curveOpt = gme::gradient::CurveFactory::createCurve(cmd.curve_type, params);

    if (!curveOpt) {
        std::fprintf(stderr, "WARNING FadeRegistry: unknown curve type '%s' "
                     "(fade_id=%s)\n", cmd.curve_type.c_str(), cmd.fade_id.c_str());
        if (tickThreadContext_) {
            pushStatusFromTick(gme::signal::StatusKind::FadeError,
                               cmd.fade_id, "unknown_curve_type");
        } else {
            emitStatusDirect(gme::signal::StatusKind::FadeError,
                             cmd.fade_id, "unknown_curve_type");
        }
        return;
    }

    // --- Step 2: Resolve start_mtc_ms sentinel (FR-016) ---
    long start_ms = (cmd.start_mtc_ms == -1)
                    ? mtcSource_.getMtcMs()
                    : cmd.start_mtc_ms;

    // --- Step 3: Supersede detection (FR-014) ---
    // effective_start defaults to the command's start_value; overridden below
    // when a supersede occurs so the new fade begins at the last OSC position
    // of the fade it replaces, producing a seamless transition.
    float effective_start = cmd.start_value;

    const std::string key = oscKey(cmd.osc_host, cmd.osc_port, cmd.osc_path);
    auto oit = osc_index_.find(key);
    if (oit != osc_index_.end()) {
        const std::string superseded_id = oit->second;
        // Emit supersede error BEFORE removing (so fade_id is still valid)
        if (tickThreadContext_) {
            pushStatusFromTick(gme::signal::StatusKind::FadeError,
                               superseded_id, "superseded");
        } else {
            emitStatusDirect(gme::signal::StatusKind::FadeError,
                             superseded_id, "superseded");
        }
        // Inherit the outgoing fade's last sent OSC value so the new fade
        // starts from the current parameter position rather than jumping.
        auto fit = fades_.find(superseded_id);
        if (fit != fades_.end()) {
            effective_start = fit->second->last_sent_value;
            if (fit->second->osc_target) {
                lo_address_free(fit->second->osc_target);
                fit->second->osc_target = nullptr;
            }
            fades_.erase(fit);
        }
        osc_index_.erase(oit);
    }

    // --- Step 4: Build lo_address ---
    lo_address addr = gme::osc::makeAddress(cmd.osc_host, cmd.osc_port);
    if (!addr) {
        std::fprintf(stderr, "WARNING FadeRegistry: lo_address_new failed for "
                     "%s:%d (fade_id=%s)\n",
                     cmd.osc_host.c_str(), cmd.osc_port, cmd.fade_id.c_str());
        if (tickThreadContext_) {
            pushStatusFromTick(gme::signal::StatusKind::FadeError,
                               cmd.fade_id, "osc_address_failed");
        } else {
            emitStatusDirect(gme::signal::StatusKind::FadeError,
                             cmd.fade_id, "osc_address_failed");
        }
        return;
    }

    // --- Step 5: Construct and insert ActiveFade ---
    auto fade = std::make_unique<ActiveFade>();
    fade->fade_id      = cmd.fade_id;
    fade->curve        = std::move(*curveOpt);
    fade->osc_target   = addr;
    fade->osc_path     = cmd.osc_path;
    fade->osc_host     = cmd.osc_host;
    fade->osc_port     = cmd.osc_port;
    fade->start_value  = effective_start;   // inherited from superseded fade, or cmd.start_value
    fade->end_value    = cmd.end_value;
    fade->start_mtc_ms = start_ms;
    fade->duration_ms  = cmd.duration_ms;
    fade->last_sent_value = effective_start;
    // crossfade_partner remains nullptr (Phase 7)

    osc_index_[key]           = cmd.fade_id;
    fades_[cmd.fade_id]       = std::move(fade);
}

// ---------------------------------------------------------------------------
// cancelFade (FR-008)
// ---------------------------------------------------------------------------

void FadeRegistry::cancelFade(const std::string& fade_id, bool snap_to_end) {
    auto it = fades_.find(fade_id);
    if (it == fades_.end()) {
        std::fprintf(stderr, "WARNING FadeRegistry: cancelFade: fade_id '%s' "
                     "not found\n", fade_id.c_str());
        return;
    }

    if (snap_to_end) {
        // Send one final OSC at end_value (FR-008)
        auto& fade = *it->second;
        oscSend_(fade.osc_target, fade.osc_path.c_str(), fade.end_value);
    }

    removeFade(fade_id);
}

// ---------------------------------------------------------------------------
// cancelAll (FR-009)
// ---------------------------------------------------------------------------

void FadeRegistry::cancelAll() {
    for (auto& kv : fades_) {
        if (kv.second && kv.second->osc_target) {
            lo_address_free(kv.second->osc_target);
            kv.second->osc_target = nullptr;
        }
    }
    fades_.clear();
    osc_index_.clear();
}

// ---------------------------------------------------------------------------
// tick (FR-001, FR-002, FR-003, FR-004, FR-005, FR-006a)
// ---------------------------------------------------------------------------

void FadeRegistry::tick(long mtc_ms) {
    // Collect fades to remove after iteration (avoid modifying map during loop)
    std::vector<std::string> to_remove;

    for (auto& kv : fades_) {
        ActiveFade& fade = *kv.second;

        // --- Compute t (FR-001) ---
        float t;
        if (fade.duration_ms <= 0.0f) {
            t = 1.0f;  // immediate completion
        } else {
            float raw = static_cast<float>(mtc_ms - fade.start_mtc_ms)
                        / fade.duration_ms;
            t = std::max(0.0f, std::min(1.0f, raw));
        }

        // --- Evaluate curve and compute value (FR-001) ---
        const double curve_val = fade.curve->evaluate(static_cast<double>(t));
        const float value = fade.start_value
            + (fade.end_value - fade.start_value)
            * static_cast<float>(curve_val);

        // --- Send OSC (FR-002) ---
        const int ret = oscSend_(fade.osc_target, fade.osc_path.c_str(), value);

        // --- Update last_sent_value (FR-003) ---
        fade.last_sent_value = value;

        // --- OSC failure tracking (FR-006a) ---
        if (ret != 0) {
            ++fade.consecutive_osc_failures;
            if (fade.consecutive_osc_failures >= kOscFailureThreshold) {
                pushStatusFromTick(gme::signal::StatusKind::FadeError,
                                   fade.fade_id, "osc_send_failed");
                to_remove.push_back(fade.fade_id);
                continue;
            }
        } else {
            fade.consecutive_osc_failures = 0;
        }

        // --- Completion detection (FR-004, FR-005) ---
        if (t >= 1.0f) {
            fade.completed = true;
            pushStatusFromTick(gme::signal::StatusKind::FadeComplete,
                               fade.fade_id, "");
            to_remove.push_back(fade.fade_id);
        }
    }

    // --- Remove completed/errored fades ---
    for (const auto& id : to_remove) {
        removeFade(id);
    }
}

} // namespace engine
} // namespace gme
