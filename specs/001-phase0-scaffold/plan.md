# Implementation Plan: Phase 0 — Project Scaffold

**Branch**: `001-phase0-scaffold` | **Date**: 2026-04-10 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-phase0-scaffold/spec.md`

## Summary

Bootstrap the `gradient-motion-engine` repository with a working CMake build system, two compilation targets (static library `libgradient_motion` and daemon executable `gradient-motiond`), an application skeleton following the VideoComposer lifecycle pattern, CuemsLogger integration for structured logging, and MTC receiver integration as git submodules. The source layout follows the README architecture: `gme::*` namespace modules inside `libgradient_motion`, with daemon-specific code separate. Configuration parsing from XML is deferred — Phase 0 uses CLI arguments only.

## Technical Context

**Language/Version**: C++17 (GCC, `-Wall -O3 -pthread`)
**Primary Dependencies**:
- `librtmidi-dev` >= 5.0 — MIDI reception (for mtcreceiver submodule)
- `libasound2-dev` — ALSA backend for RtMidi
- `libnng-dev` — NNG bus (declared, not linked in Phase 0)
- `nlohmann-json3-dev` — JSON parsing (declared, not linked in Phase 0)
- `libtinyxml2-dev` — XML config parsing (declared, not linked in Phase 0)
- `liblo-dev` — OSC sending (declared, not linked in Phase 0)
**Storage**: N/A
**Testing**: CTest + custom test executables (no test framework in Phase 0; tests are Phase 1+)
**Target Platform**: Linux (Debian-based), systemd, x86_64
**Project Type**: Static library (`libgradient_motion`) + daemon executable (`gradient-motiond`)
**Performance Goals**: Build under 2 minutes; startup under 1 second; shutdown under 1 second
**Constraints**: Zero external runtime dependencies beyond system packages; all shared CUEMS components as git submodules
**Scale/Scope**: Single-node deployment, one process per CUEMS node

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Phase 0 Status | Notes |
|-----------|---------------|-------|
| I. Deterministic Evaluation | N/A | No evaluation logic in Phase 0 (scaffold only) |
| II. Modular Architecture | PASS | Library organized by `gme::*` namespace modules (time, motion, gradient, signal, osc, engine) each in its own directory. Submodules (`mtcreceiver`, `cuemslogger`) compiled as independent subdirectory targets with no circular dependencies |
| III. Library-First | PASS | `libgradient_motion` is a standalone static library target. The daemon `gradient-motiond` links it but the library has no daemon dependencies |
| IV. Real-Time Safety | N/A | No hot-path code in Phase 0 |
| V. Protocol-Agnostic Core | PASS | OSC module (`src/osc/`) is a separate directory within the library, isolated from evaluation modules. Even at scaffold level the structure enforces this separation |
| VI. Documentation Standards | PASS | All public classes (GradientEngineApplication, ConfigurationManager) MUST have Doxygen-style docstrings. Phase 0 creates the skeleton — docstrings required from the start |

**Gate result**: PASS — no violations. Proceed to Phase 0 research.

## Project Structure

### Documentation (this feature)

```text
specs/001-phase0-scaffold/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
gradient-motion-engine/
├── CMakeLists.txt                       # Root build: C++17, two targets, submodule checks
├── src/
│   ├── CMakeLists.txt                   # libgradient_motion static library target
│   ├── time/                            # gme::time — timecode, clocks, scheduling
│   │   └── (empty in Phase 0)
│   ├── motion/                          # gme::motion — trajectories and spatial evaluation
│   │   └── (empty in Phase 0)
│   ├── gradient/                        # gme::gradient — keyframes and interpolation
│   │   └── (empty in Phase 0)
│   ├── signal/                          # gme::signal — evaluated value representation
│   │   └── (empty in Phase 0)
│   ├── osc/                             # gme::osc — OSC encoding and transport
│   │   └── (empty in Phase 0)
│   └── engine/                          # gme::engine — orchestration and execution pipeline
│       └── (empty in Phase 0)
├── daemon/                              # gradient-motiond daemon-specific code
│   ├── main.cpp                         # Entry point: parse CLI, instantiate app, run
│   ├── GradientEngineApplication.h          # Application lifecycle orchestrator
│   ├── GradientEngineApplication.cpp
│   ├── logging.h                        # Compile-time logging abstraction (#ifdef HAVE_CUEMS_LOGGER)
│   └── config/
│       ├── ConfigurationManager.h       # CLI arg storage (XML parsing deferred)
│       └── ConfigurationManager.cpp
├── cuemslogger/                         # Git submodule → github.com/stagesoft/cuemslogger
│   │                                    #   (optional: controlled by ENABLE_CUEMS_LOGGER flag)
│   ├── cuemslogger.h
│   ├── cuemslogger.cpp
│   └── CMakeLists.txt
├── mtcreceiver/                         # Git submodule → github.com/stagesoft/mtcreceiver
│   ├── mtcreceiver.h                    #   pinned to commit 63ce3de
│   ├── mtcreceiver.cpp
│   └── CMakeLists.txt
└── tests/                               # Empty in Phase 0; populated in Phase 1+
```

**Structure Decision**: The source layout mirrors the README architecture. `src/` contains the `libgradient_motion` library organized by `gme::*` namespace modules — each module gets its own subdirectory. `daemon/` contains `gradient-motiond`-specific code (application lifecycle, CLI, configuration) that is NOT part of the library. This enforces Constitution Principle III (Library-First): daemon code cannot leak into the library because they live in separate directory trees. Submodules (`cuemslogger/`, `mtcreceiver/`) are at repo root level as peer siblings so that mtcreceiver's `#include "../cuemslogger/cuemslogger.h"` relative path resolves correctly.

## Key Research Findings

*(Full details in [research.md](research.md))*

1. **CuemsLogger API**: Syslog-based singleton (`CuemsLogger::getLogger()`). Levels: Emergency, Alert, Critical, Error, Warning, Notice, Info, Debug. Constructor takes a `slug` string used as syslog identifier prefix (`Cuems:<slug>`). No external dependencies beyond `<syslog.h>` and `<filesystem>`.

2. **MtcReceiver build integration**: The submodule's CMakeLists.txt uses `${RTMIDI_INCLUDE_DIRS}` and `${RTMIDI_LIBRARIES}` — these must be set by the root CMakeLists via `pkg_check_modules(RTMIDI REQUIRED rtmidi)` before `add_subdirectory(mtcreceiver)`.

3. **HAVE_CUEMS_LOGGER linkage**: mtcreceiver.h uses `#ifdef HAVE_CUEMS_LOGGER` to switch between CuemsLogger and a built-in stub. mtcreceiver always uses its stub (avoids needing `cuems_errors.h` shim). GradientEngine's daemon code uses a project-internal `daemon/logging.h` abstraction that selects CuemsLogger (when `ENABLE_CUEMS_LOGGER=ON`, defines `-DHAVE_CUEMS_LOGGER`) or a stdlib `<iostream>` fallback (when OFF). This allows building without the cuemslogger submodule for standalone development/testing.

4. **VideoComposer lifecycle pattern**: `Application::initialize(argc, argv) → run() → shutdown()`. The `main.cpp` instantiates the application on the stack, calls `initialize()`, then `run()`, returning its exit code.

5. **Submodule directory placement**: Both submodules MUST be at the same directory level (repo root) so that mtcreceiver's relative include `../cuemslogger/cuemslogger.h` resolves.

6. **Library vs daemon separation**: `libgradient_motion` (built from `src/`) contains only reusable library code organized by `gme::*` modules. `gradient-motiond` (built from `daemon/`) contains the application skeleton, CLI parsing, and lifecycle management. The daemon links the library but the library has zero knowledge of the daemon.

## Complexity Tracking

No constitution violations to justify.
