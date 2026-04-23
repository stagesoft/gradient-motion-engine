# Implementation Plan: Phase 4 — Fade Registry, Tick Loop & OSC Sender

**Branch**: `006-fade-registry-tick-loop` | **Date**: 2026-04-23 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `/specs/006-fade-registry-tick-loop/spec.md`

## Summary

Phase 4 assembles the evaluation core of `libgradient_motion`. It adds four new components — `ActiveFade`, `FadeRegistry`, `GradientEngine` (in `gme::engine`) and `OscSender` (in `gme::osc`) — and extends `NngBusClient` with a status worker thread. The MTC tick thread drains `LockFreeQueue`, materialises `ActiveFade` entries via `FadeRegistry::addFade`, calls `FadeRegistry::tick(mtc_ms)` every quarter frame (200 Hz at 25 fps), sends one UDP OSC float per active fade via `OscSender`, and pushes `(fade_complete | fade_error, fade_id, reason)` tuples onto an outbound SPSC queue consumed by the status worker thread in `NngBusClient`. All prior phases (1: gradient curves, 2: MTC tick source, 3: NNG bus client + LockFreeQueue) are prerequisites and assumed complete.

## Technical Context

**Language/Version**: C++17 (GCC, `-Wall -O3 -pthread`)  
**Primary Dependencies**: liblo (OSC sending), NNG 1.10.1 (already linked), nlohmann-json (already linked), RtMidi via mtcreceiver submodule (already linked)  
**Storage**: N/A — all state in-memory (`std::unordered_map` inside `FadeRegistry`, fixed SPSC queue for status)  
**Testing**: CTest (`cmake --build build --target test`); unit labels and integration label (BUILD_DAEMON=ON); test_fade_registry.cpp (new, unit); loopback OSC benchmark (new, integration)  
**Target Platform**: Linux (systemd service, localhost OSC targets)  
**Project Type**: Static library (`libgradient_motion`) + daemon (`gradient-motiond`); Phase 4 adds library modules + daemon extension  
**Performance Goals**: ≤ 2 ms tick duration (p99) with 50 concurrent active fades at 200 Hz (SC-007); zero heap allocation per evaluation frame in steady state (constitution)  
**Constraints**: Tick thread must never block on NNG I/O (FR-006b); lo_send is UDP-to-loopback — non-blocking in practice; curve LUT is pre-built at addFade, not per-tick; OSC target address pre-built at addFade  
**Scale/Scope**: Up to 50 concurrent active fades; SPSC status queue capacity 64 (matches inbound queue)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Deterministic Evaluation | ✅ PASS | `t = clamp(...)` + `curve->evaluate(t)` is a pure function of (mtc_ms, config). OSC delivery is transport-layer only and does not feed back into evaluation. |
| II. Modular Architecture | ✅ PASS | `gme::engine` imports `gme::gradient` + `gme::signal` + `gme::time` + `gme::osc`. `gme::osc` imports only liblo. No circular deps. Adding/removing engine module requires no changes to gradient or time. |
| III. Library-First | ✅ PASS | `ActiveFade`, `FadeRegistry`, `GradientEngine`, `OscSender` all live in `src/`. Status worker extension lives in `NngBusClient` (`daemon/comms/`) — correctly daemon-side. |
| IV. Real-Time Safety | ⚠️ JUSTIFIED | **Steady-state tick path is allocation-free**: LUT lookup + float arithmetic, pre-built lo_address, lo_send to loopback (non-blocking). The one-time `addFade` cost (CurveFactory LUT construction, lo_address_new, map insert) is acceptable — not steady-state. See Complexity Tracking. |
| V. Protocol-Agnostic Core | ✅ PASS | `OscSender` is isolated in `gme::osc`. Core evaluation produces values; serialisation to OSC is a pluggable output step. |
| VI. Documentation Standards | ✅ REQUIRED | All new public headers must carry Doxygen-compatible docstrings (brief, params, errors, example). `ActiveFade.h`, `FadeRegistry.h`, `GradientEngine.h`, `OscSender.h` are in scope. |

## Project Structure

### Documentation (this feature)

```text
specs/006-fade-registry-tick-loop/
├── plan.md              ← This file
├── research.md          ← Phase 0 output
├── data-model.md        ← Phase 1 output
├── quickstart.md        ← Phase 1 output
├── contracts/           ← Phase 1 output
│   ├── fade-registry-api.md
│   ├── gradient-engine-api.md
│   └── osc-sender-api.md
└── tasks.md             ← Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
src/                                    ← libgradient_motion (static library)
├── gradient/                           ← gme::gradient — COMPLETE (Phase 1)
│   ├── Curve.h
│   ├── LinearCurve.h / SigmoidCurve.h/.cpp
│   ├── EaseInCurve.h / EaseOutCurve.h / SCurve.h
│   ├── BezierCurve.h/.cpp
│   ├── ScaledCurve.h / ResampledCurve.h/.cpp
│   ├── CrossfadePair.h
│   └── CurveFactory.h/.cpp
├── time/                               ← gme::time — COMPLETE (Phase 2)
│   └── MtcTickSource.h/.cpp
├── signal/                             ← gme::signal — COMPLETE (Phase 3)
│   ├── FadeCommand.h/.cpp
│   └── LockFreeQueue.h
├── engine/                             ← gme::engine — NEW (Phase 4)
│   ├── ActiveFade.h                    ← data struct; Phase 4 adds definition
│   ├── FadeRegistry.h/.cpp             ← NEW: addFade, cancelFade, cancelAll, tick
│   └── GradientEngine.h/.cpp          ← NEW: wires 3 threads + tick callback
└── osc/                                ← gme::osc — NEW (Phase 4)
    └── OscSender.h/.cpp               ← NEW: thin liblo UDP wrapper

daemon/                                 ← gradient-motiond (executable)
└── comms/
    └── NngBusClient.h/.cpp            ← EXTENDED: add status worker thread + SPSC queue

tests/
├── test_curves.cpp                     ← COMPLETE (Phase 1)
├── test_mtc_tick_source.cpp            ← COMPLETE (Phase 2)
├── test_nng_parse.cpp                  ← COMPLETE (Phase 3)
├── test_lockfree_queue.cpp             ← COMPLETE (Phase 3)
├── test_nng_integration.cpp            ← COMPLETE (Phase 3)
├── test_fade_registry.cpp              ← NEW (Phase 4): unit — mock MTC, registry, cancel
└── test_fade_registry_bench.cpp        ← NEW (Phase 4): OSC loopback bench, SC-007
```

**Structure Decision**: Single project, library + daemon pattern established in Phase 0. Phase 4 extends the existing tree, adding `src/engine/` and filling `src/osc/` (previously a stub). Test files follow the existing `test_*.cpp` convention compiled via `tests/CMakeLists.txt`.

## Complexity Tracking

| Item | Why Needed | Simpler Alternative Rejected Because |
|------|-----------|-------------------------------------|
| lo_send on tick thread (Principle IV grey area) | UDP OSC is the only delivery path to AudioPlayer/VideoComposer; the tick thread is the only thread that knows the current per-fade value. Moving lo_send to a separate thread would require a second value-staging queue per fade, adding latency and complexity. | Loopback UDP sends on Linux are non-blocking in practice (kernel socket buffer ~212 KB, message size ~60 bytes). Under load tests (50 fades × 60-byte messages = 3 KB/tick, 200 Hz = 600 KB/s) this is far below the loopback bandwidth. Documented assumption in research.md Decision 2. |
| CurveFactory (LUT) + lo_address_new on tick thread at addFade | Both happen once per fade start, not per-frame. Constitution says "startup and configuration loading are exempt" from the zero-allocation budget. addFade is conceptually a configuration event (registering a new signal). | These are one-time costs. Deferring to a pre-tick thread would require a "pending fades" staging area and adds a dispatch cycle of latency between command receipt and first OSC output. Acceptable: LUT build takes ~10 µs, lo_address_new is a single malloc. |
