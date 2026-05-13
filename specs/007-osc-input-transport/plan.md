<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Implementation Plan: Phase H — OSC Input Transport

**Branch**: `feat/osc-input-transport` (spec directory `specs/007-osc-input-transport/`) | **Date**: 2026-05-13 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/007-osc-input-transport/spec.md`

## Summary

Replace the daemon's NNG bus-client inbound command path with a localhost UDP OSC listener. The change is transport-only: every command-record shape (`FadeCommand`), every queue (`LockFreeQueue`), every motion type (`FadeMotion`, `VectorMotion<N>` design), every curve (Bezier, future spline types), and every output path (`OscSender` to `/volmaster` etc.) is preserved unchanged. What changes is who produces the `FadeCommand` records that flow into the tick thread: instead of the NNG bus client thread parsing JSON, an `OscServer` based on `liblo`'s server-mode receives `/gradient/start_fade`, `/gradient/cancel_motion`, and `/gradient/cancel_all` messages on `127.0.0.1:7100` (configurable) and pushes parsed records into the same lock-free queue. Status emission back to the network is removed wholesale — its consumer in cuems-engine was already dead code (verified during Phase E investigation) and the future fade-progress UI feature lands in cuems-engine's `loop_fadeCue` per the existing audio/video/dmx progress pattern. The companion change in `cuems-engine` (separate repo, branch `feat/gradient-osc-transport`) adds a `GradientPlayer.py` modeled on `VideoPlayer.py` / `DmxPlayer.py` and rewires `_handle_fade_action` to dispatch via it; this plan covers only the C++ daemon side.

## Technical Context

**Language/Version**: C++17 (GCC, `-Wall -O3 -pthread`) — unchanged from earlier phases.
**Primary Dependencies**: `liblo` (now used on both sender and server sides — already linked), `nlohmann-json` (curve_params parsing, unchanged), `mtcreceiver` submodule pinned at `59fc76e` (unchanged), `cuemslogger` (unchanged). **Removed**: `libnng` / `libnng-dev`.
**Storage**: N/A — all state in-memory. The OSC server reuses the existing fixed-size `LockFreeQueue<FadeCommand, 64>` for handoff to the tick thread.
**Testing**: `ctest` against the project's existing GoogleTest harness. New tests: `tests/test_osc_parse.cpp` (parse-level), `tests/test_osc_server_integration.cpp` (loopback wire smoke). Deleted: `tests/test_nng_parse.cpp`, `tests/test_nng_integration.cpp`.
**Target Platform**: Debian 13 / Ubuntu 24.04 server (systemd-managed daemon). Same as Phase 3.
**Project Type**: Single C++ project — `libgradient_motion` (core library) + `gradient-motiond` (thin daemon consumer). Constitution Principle III holds.
**Performance Goals**: Sub-millisecond per-command latency from `lo_send` on the local NodeEngine side to `LockFreeQueue::push` on the daemon side (spec SC-002). Per-tick OSC output budget unchanged from Phase 6 (< 1 ms per pipeline tick, Constitution §"Performance & Safety Standards").
**Constraints**: Loopback-only bind (FR-002). Zero heap allocations in the tick-thread consumer path (Constitution Principle IV) — the OSC server thread is the producer and may allocate for `nlohmann::json::parse` on `curve_params_json`, same as the Phase 3 NNG path. Daemon shutdown must complete within 2 s (Constitution §"Performance & Safety Standards").
**Scale/Scope**: Single-node deployment (the daemon and its NodeEngine always colocate). ~64 in-flight motions max (queue capacity). Source diff: roughly +600 / –600 lines centered on `daemon/comms/` and `tests/`.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

The constitution version at planning time is **1.2.0** (ratified 2026-04-10, last amended 2026-05-13). Evaluated against the seven principles plus the Development Workflow section:

| Principle | Verdict | Justification |
|---|---|---|
| **I. Deterministic Evaluation** | PASS | Evaluation is read from the lock-free queue. Producer side change (NNG-thread → liblo-thread) does not feed back into evaluation. Transport-layer jitter remains transport-layer jitter (Principle I bullet 3). |
| **II. Modular Architecture** | PASS, IMPROVED | `gme::signal` keeps `FadeCommand`+`LockFreeQueue`+`ParseResult` enum. New parser `parseFadeOscCommand` is added in `gme::signal` (not `daemon/comms`), keeping the wire-format parse logic library-side. The `OscServer` wrapper lives in `daemon/comms` and is daemon-only — it knows about transport mechanics but not about motion logic. No circular dependencies introduced. |
| **III. Library-First** | PASS, IMPROVED | NNG dependency leaves the library's transitive closure entirely. liblo was already a library dep on the sender side; reusing it on the receiver side does not increase the library's external surface. The daemon binary continues to be a thin consumer. |
| **IV. Real-Time Safety** | PASS | Tick-thread hot path is unchanged. OSC producer thread is wait-free wrt the tick thread (SPSC `LockFreeQueue::push`). Producer-thread allocations (json parse) are tolerated, same as Phase 3. |
| **V. Protocol-Agnostic Core** | PASS, STRENGTHENED | Evaluation and motion logic remain transport-independent. After Phase H lands, the library compiles and links without any wire-format-specific dependency (`gme::signal::FadeCommand` is a pure record; the parsers — JSON and OSC — are separate translation units). |
| **VI. Documentation Standards** | PASS, REQUIRES IMPLEMENTATION CARE | New public headers (`OscServer.h`, `parseFadeOscCommand.h`) ship with complete docstrings per the contract files in [`contracts/`](contracts/). Doxygen build on CI must remain warning-free — verified during implementation. |
| **VII. Extensibility via Abstraction** | PASS | `IMotion` hierarchy and `CurveFactory` are not touched. The OSC namespace `/gradient/*` is open to future commands (`start_crossfade`, `start_vector`) without altering the parser's existing-command paths. |
| **Development Workflow — Spec & Planning Layout** | PASS | This feature lives in `specs/007-osc-input-transport/`. The pre-existing planning notes that motivated it (`specs/planning/phase-h-osc-refactor-plan.md` and `PHASE-H-HANDOFF-NOTES.md`) live in `specs/planning/` per the constitution's amendment. No artifacts leak into `docs/`. |

**Result: PASS** with no required justifications. Complexity Tracking section is empty.

Re-check after Phase 1 design completion (data-model.md, contracts/*, quickstart.md): no new violations introduced. The `OscServer::Impl` PIMPL pattern in the contract header avoids exposing `lo_server_thread` to library consumers, supporting Principle II (Modular Architecture, internal-implementation isolation). The header forward-declares `Impl` and stores `std::unique_ptr<Impl>` so the daemon's translation unit owns the liblo include; library consumers who don't run a server do not transitively include `<lo/lo.h>`. Constitution Check still PASS post-design.

## Project Structure

### Documentation (this feature)

```text
specs/007-osc-input-transport/
├── plan.md                # this file
├── spec.md                # /speckit-specify output (already written)
├── research.md            # /speckit-plan Phase 0 output (already written)
├── data-model.md          # /speckit-plan Phase 1 output (already written)
├── quickstart.md          # /speckit-plan Phase 1 output (already written)
├── contracts/
│   ├── gradient_osc.md           # canonical OSC wire contract
│   ├── OscServer.h               # daemon-side listener contract
│   └── parseFadeOscCommand.h     # parser contract
├── checklists/
│   └── requirements.md    # /speckit-specify quality gate (all items pass)
└── tasks.md               # /speckit-tasks output (NOT produced by /speckit-plan)
```

### Source Code (repository root)

Single C++ project, no frontend / backend split. The directories below are the **existing** layout from Phases 1–6; Phase H modifies a small subset of them.

```text
src/
├── engine/                # GradientEngine — tick loop driver
│   ├── GradientEngine.cpp # MODIFIED: replace nngClient_ with oscServer_; otherwise unchanged shape
│   └── GradientEngine.h   # MODIFIED: header swap on member type, no API change for consumers
├── gradient/              # CurveFactory, BezierCurve, curve evaluation — UNTOUCHED
├── motion/                # IMotion, FadeMotion, MotionRegistry, MotionFactory — UNTOUCHED
├── osc/                   # OscSender (existing liblo wrapper for output) — UNTOUCHED
├── signal/
│   ├── FadeCommand.{h,cpp}        # KEPT shape; JSON parser path deleted after OSC parser proves green
│   ├── LockFreeQueue.h            # UNTOUCHED — reused for the OSC-thread → tick-thread handoff
│   ├── parseFadeOscCommand.{h,cpp}  # NEW (Phase H)
│   └── StatusEmitRequest.h        # DELETED — no status emission in Phase H
├── time/                  # MtcTickSource — UNTOUCHED
└── CMakeLists.txt         # MODIFIED: drop nng linkage from libgradient_motion (if any was there)

daemon/
├── main.cpp                          # MODIFIED: --nng-url removed, --osc-port added
├── GradientEngineApplication.{cpp,h} # MODIFIED: build OscServer instead of NngBusClient
├── config/                           # MODIFIED: surface gradient_osc_port from settings.xml
├── logging.h                         # UNTOUCHED
└── comms/
    ├── NngBusClient.{cpp,h}   # DELETED
    └── OscServer.{cpp,h}      # NEW (Phase H)

tests/
├── test_curves.cpp                  # UNTOUCHED
├── test_fade_motion.cpp             # UNTOUCHED
├── test_lockfree_queue.cpp          # UNTOUCHED
├── test_motion_registry.cpp         # UNTOUCHED
├── test_motion_registry_bench.cpp   # UNTOUCHED
├── test_mtc_tick_source.cpp         # UNTOUCHED
├── test_nng_integration.cpp         # DELETED
├── test_nng_parse.cpp               # DELETED
├── test_osc_parse.cpp               # NEW (Phase H)
└── test_osc_server_integration.cpp  # NEW (Phase H)

debian/
└── control                # MODIFIED: drop libnng-dev / libnng1; liblo-dev / liblo7 stay
```

**Structure Decision**: Single project (`libgradient_motion` + `gradient-motiond`). This is the layout used by every feature 001–006. Phase H does not change directory structure — it deletes two files in `daemon/comms/` and `src/signal/`, adds three files (one each in `daemon/comms/`, `src/signal/`, and a renamed `src/signal/parseFadeOscCommand.cpp`), and updates `CMakeLists.txt` + `debian/control` accordingly. No new top-level directories.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|

*(Empty — Constitution Check passes with no justifications required. The design respects every principle and improves alignment with Principles II, III, and V.)*
