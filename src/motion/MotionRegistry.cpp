/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file MotionRegistry.cpp
 * @brief MotionRegistry implementation — motion lifecycle, tick evaluation,
 *        cancel, supersede, duplicate-id rejection, and status emission.
 */

#include "motion/MotionRegistry.h"
#include "motion/MotionFactory.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace gme {
namespace motion {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MotionRegistry::MotionRegistry(
    const gme::time::MtcTickSource& mtcSource,
    std::function<void(gme::signal::StatusKind,
                       const std::string&,
                       const std::string&)> statusDirect,
    OscSendFn oscSend)
    : mtcSource_(mtcSource)
    , statusDirect_(std::move(statusDirect))
    , oscSend_(std::move(oscSend))
{}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void MotionRegistry::removeMotion(const std::string& motion_id) {
    auto it = motions_.find(motion_id);
    if (it == motions_.end()) return;

    const std::string& key = it->second->osc_key;
    auto oit = osc_index_.find(key);
    if (oit != osc_index_.end() && oit->second == motion_id) {
        osc_index_.erase(oit);
    }

    motions_.erase(it);
}

void MotionRegistry::emitStatus(gme::signal::StatusKind kind,
                                const std::string& motion_id,
                                const std::string& reason) {
    if (statusDirect_) {
        statusDirect_(kind, motion_id, reason);
    }
}

// ---------------------------------------------------------------------------
// apply — command dispatch
// ---------------------------------------------------------------------------

void MotionRegistry::apply(gme::signal::FadeCommand& cmd) {
    using Type = gme::signal::FadeCommand::Type;
    switch (cmd.type) {
        case Type::START_FADE: {
            MotionFactory::Context ctx{
                mtcSource_,
                oscSend_,
                [this](gme::signal::StatusKind k,
                       const std::string& id,
                       const std::string& r) {
                    emitStatus(k, id, r);
                }
            };
            auto motion = MotionFactory::fromCommand(cmd, ctx);
            if (motion) addMotion(std::move(motion));
            break;
        }
        case Type::CANCEL_MOTION:
            cancelMotion(cmd.motion_id, /*snap_to_end=*/false);
            break;
        case Type::CANCEL_ALL:
            cancelAll();
            break;
        case Type::START_CROSSFADE:
            std::fprintf(stderr, "INFO MotionRegistry: START_CROSSFADE dropped "
                         "(deferred to future feature, motion_id=%s)\n",
                         cmd.motion_id.c_str());
            break;
    }
}

// ---------------------------------------------------------------------------
// addMotion
// ---------------------------------------------------------------------------

void MotionRegistry::addMotion(std::unique_ptr<IMotion> m) {
    // --- Check 1: Duplicate motion_id rejection ---
    if (motions_.count(m->motion_id)) {
        emitStatus(gme::signal::StatusKind::MotionError,
                   m->motion_id, "duplicate_motion_id");
        return;
    }

    // --- Check 2: osc_key supersede ---
    auto oit = osc_index_.find(m->osc_key);
    if (oit != osc_index_.end()) {
        const std::string superseded_id = oit->second;
        emitStatus(gme::signal::StatusKind::MotionError,
                   superseded_id, "superseded");

        auto fit = motions_.find(superseded_id);
        if (fit != motions_.end()) {
            m->inheritFrom(*fit->second);
            motions_.erase(fit);
        }
        osc_index_.erase(oit);
    }

    // --- Check 3: Insert ---
    const std::string key       = m->osc_key;
    const std::string motion_id = m->motion_id;
    osc_index_[key]             = motion_id;
    motions_[motion_id]         = std::move(m);
}

// ---------------------------------------------------------------------------
// cancelMotion
// ---------------------------------------------------------------------------

void MotionRegistry::cancelMotion(const std::string& motion_id, bool snap_to_end) {
    auto it = motions_.find(motion_id);
    if (it == motions_.end()) {
        std::fprintf(stderr, "WARNING MotionRegistry: cancelMotion: motion_id '%s' "
                     "not found\n", motion_id.c_str());
        return;
    }

    if (snap_to_end) {
        it->second->sendSnapToEnd();
    }

    removeMotion(motion_id);
}

// ---------------------------------------------------------------------------
// cancelAll
// ---------------------------------------------------------------------------

void MotionRegistry::cancelAll() {
    motions_.clear();
    osc_index_.clear();
}

// ---------------------------------------------------------------------------
// tick
// ---------------------------------------------------------------------------

void MotionRegistry::tick(long mtc_ms) {
    std::vector<std::string> to_remove;

    for (auto& kv : motions_) {
        IMotion& m = *kv.second;

        EvalResult r = m.evalAndSend(mtc_ms);

        if (r.failed) {
            ++m.consecutive_osc_failures;
            if (m.consecutive_osc_failures >= kOscFailureThreshold) {
                const char* reason = r.failure_reason
                                     ? r.failure_reason
                                     : "osc_send_failed";
                emitStatus(gme::signal::StatusKind::MotionError,
                           m.motion_id, reason);
                to_remove.push_back(m.motion_id);
                continue;
            }
        } else {
            m.consecutive_osc_failures = 0;
        }

        if (r.completed) {
            m.completed = true;
            emitStatus(gme::signal::StatusKind::MotionComplete, m.motion_id, "");
            to_remove.push_back(m.motion_id);
        }
    }

    for (const auto& id : to_remove) {
        removeMotion(id);
    }
}

} // namespace motion
} // namespace gme
