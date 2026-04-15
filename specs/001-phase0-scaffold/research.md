# Research: Phase 0 — Project Scaffold

**Date**: 2026-04-10
**Feature**: `001-phase0-scaffold`

## 1. CuemsLogger API and Integration

**Decision**: Use CuemsLogger as a git submodule behind a CMake option `ENABLE_CUEMS_LOGGER` (default ON). When enabled, compiled as a static library subdirectory target. When disabled, fall back to a stdlib `<iostream>` wrapper. Initialize in GradientEngineApplication constructor with slug `"GradientEngine"`. All daemon code accesses logging through `daemon/logging.h` which selects the backend at compile time.

**Rationale**: CuemsLogger is the standard logging component across all CUEMS C++ services. It provides syslog-based logging with consistent formatting. However, making it optional via a build flag allows standalone builds outside the CUEMS ecosystem (development, testing, reuse) without requiring the submodule.

**API summary** (from `cuemslogger.h`):
- Constructor: `CuemsLogger(const string slug = "CuemsLog")` — opens syslog with identifier `Cuems:<slug>`
- Singleton access: `CuemsLogger::getLogger()` — returns global instance
- Log levels: `logEmergency()`, `logAlert()`, `logCritical()`, `logError()`, `logWarning()`, `logNotice()`, `logInfo()`, `logDebug()`, `logOK()`
- Destructor: closes syslog
- Dependencies: `<syslog.h>`, `<filesystem>`, `<string>` — no external libraries
- Build: `add_library(cuemslogger cuemslogger.cpp)` — single source file, no `target_link_libraries`

**Log level mapping for `--log-level` CLI argument**:
- The `--log-level` argument controls which messages reach syslog. Since CuemsLogger calls `syslog()` directly (no internal filtering), log level filtering is handled by syslog configuration or by wrapping calls with a level check in GradientEngineApplication.
- Recommended approach: store the requested log level in ConfigurationManager and add a thin wrapper that checks level before calling CuemsLogger methods. This avoids modifying the shared CuemsLogger submodule.

**Alternatives considered**:
- Raw `syslog()` calls: simpler but loses consistency with other CUEMS services.
- `spdlog` or other third-party logger: adds unnecessary dependency; CUEMS ecosystem already standardized on CuemsLogger.

## 2. MtcReceiver Build Integration

**Decision**: Add mtcreceiver as a git submodule at repo root, pinned to commit `63ce3de` (RtMidi 5.x compatibility fix from stagesoft/mtcreceiver#2). Define `HAVE_CUEMS_LOGGER` so mtcreceiver uses CuemsLogger instead of its built-in stub.

**Rationale**: The upstream fix eliminates the need for any local patches. Using the real CuemsLogger ensures MTC-related log messages appear with consistent formatting.

**Build requirements** (from `mtcreceiver/CMakeLists.txt`):
- `add_library(mtcreceiver mtcreceiver.cpp)`
- Expects `${RTMIDI_INCLUDE_DIRS}` and `${RTMIDI_LIBRARIES}` to be set by parent CMake
- Root CMakeLists must: `pkg_check_modules(RTMIDI REQUIRED rtmidi)` before `add_subdirectory(mtcreceiver)`

**HAVE_CUEMS_LOGGER mechanism** (from `mtcreceiver.h:51-69`):
- When defined: `#include "../cuemslogger/cuemslogger.h"` and `#include "../cuems_errors.h"`
- When NOT defined: falls through to a built-in stub logger (inline namespace with `logError`, `logWarning`, `logInfo`)
- The relative include `../cuemslogger/cuemslogger.h` requires cuemslogger and mtcreceiver to be peer directories

**cuems_errors.h dependency**: mtcreceiver.h includes `../cuems_errors.h` when `HAVE_CUEMS_LOGGER` is defined. This file is NOT part of the cuemslogger or mtcreceiver submodules — it lives in the parent project (VideoComposer). Options:
1. Create a minimal `cuems_errors.h` in the repo root defining `CUEMS_EXIT_NO_MIDI_PORTS_FOUND`
2. Do NOT define `HAVE_CUEMS_LOGGER` for mtcreceiver — let it use the stub logger; use CuemsLogger only in GradientEngine's own daemon code

**Decision**: ~~Option 2~~ → **Revised to Option 1**: Define `HAVE_CUEMS_LOGGER` for mtcreceiver when `ENABLE_CUEMS_LOGGER=ON`, so both daemon and mtcreceiver use the same logging backend controlled by a single CMake flag. The `cuems_errors.h` dependency is resolved by a minimal shim at repo root (`cuems_errors.h`) defining only `CUEMS_EXIT_NO_MIDI_PORTS_FOUND = 1` — the sole symbol mtcreceiver references. The shim is self-contained and not a copy of VideoComposer's file.

**Implementation**:
- `cuems_errors.h` at repo root — minimal shim, one `#define`
- Root `CMakeLists.txt`: after `add_subdirectory(mtcreceiver)`, add `target_compile_definitions(mtcreceiver PRIVATE HAVE_CUEMS_LOGGER)` and `target_link_libraries(mtcreceiver PRIVATE cuemslogger)` inside `if(ENABLE_CUEMS_LOGGER)` block
- When `ENABLE_CUEMS_LOGGER=OFF`: mtcreceiver still uses its built-in stub logger unchanged

**Alternatives considered**:
- Keeping Option 2 (stub for mtcreceiver): inconsistent — MTC receiver log messages appear on stderr instead of the journal alongside daemon messages. Acceptable for Phase 0 but undesirable as a permanent state.
- Forking cuemslogger to remove the `cuems_errors.h` dependency in mtcreceiver: changes upstream shared code for one consumer's convenience. Disproportionate.

## 3. VideoComposer Lifecycle Pattern

**Decision**: Follow VideoComposer's `Application::initialize(argc, argv) → run() → shutdown()` pattern. Adapt for GradientEngine's simpler needs (no display, no layers, no renderer).

**Rationale**: Proven pattern in the CUEMS ecosystem. `main.cpp` stays minimal — construct app, initialize, run, return exit code.

**Key observations from VideoComposer** (`main.cpp`, `VideoComposerApplication.h`):
- `main()` creates app on stack, calls `initialize(argc, argv)`, then `run()`, returns result
- `initialize()` returns `bool` — false means startup failed or help was requested
- `run()` returns `int` (exit code) — contains the main event loop
- `shutdown()` called from destructor or explicitly
- `running_` bool flag checked by event loop; set false by signal handler or `quit()`
- `--help` / `-h` handling: `initialize()` prints help and returns false; `main()` checks args to distinguish help from real error

**GradientEngine adaptation**:
- `GradientEngineApplication::initialize(int argc, char** argv)`: parse CLI args (getopt_long), initialize CuemsLogger, store config values. Lives in `daemon/`, NOT in `src/` (library).
- `GradientEngineApplication::run()`: set up signal handlers (SIGTERM, SIGINT → set `running_ = false`), enter blocking wait loop (e.g., `sigwait` or `pause()`), return 0
- `GradientEngineApplication::shutdown()`: log shutdown, clean up (nothing substantive in Phase 0)

## 4. CMake Build System Design

**Decision**: Root `CMakeLists.txt` with C++17, PkgConfig for system dependencies, submodule checks, two targets.

**Build targets**:
1. `gradient_motion` — `STATIC` library from `src/` (placeholder sources with `gme::*` namespace module directories in Phase 0)
2. `gradient-motiond` — `EXECUTABLE` from `daemon/main.cpp`, `daemon/GradientEngineApplication.cpp`, `daemon/config/ConfigurationManager.cpp`. Links: `gradient_motion`, `cuemslogger`, `mtcreceiver`, `${RTMIDI_LIBRARIES}`

**Submodule presence check**: Use `if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cuemslogger/CMakeLists.txt")` to detect uninitialized submodules and emit `message(FATAL_ERROR ...)`.

**Library module structure**: `src/` contains one subdirectory per `gme::*` namespace module (time, motion, gradient, signal, osc, engine). `src/CMakeLists.txt` collects all module sources into the `gradient_motion` static library target. In Phase 0, modules contain only placeholder files; each phase populates its relevant module directory.

**Dependency declaration strategy**: All dependencies listed in spec (libnng, nlohmann-json, libtinyxml2, liblo) are declared via `pkg_check_modules` with `REQUIRED` only for Phase 0 needs (rtmidi). Others use non-required checks to validate availability without blocking the build:
- `pkg_check_modules(RTMIDI REQUIRED rtmidi)` — required (mtcreceiver)
- `pkg_check_modules(NNG nng)`, `pkg_check_modules(TINYXML2 tinyxml2)`, `pkg_check_modules(LIBLO liblo)` — checked but not REQUIRED in Phase 0
- nlohmann-json is header-only, checked via `find_package(nlohmann_json QUIET)`

**Daemon vs library targets**: The root `CMakeLists.txt` adds three subdirectories: `src/` (builds `libgradient_motion`), `cuemslogger/`, `mtcreceiver/`. It then defines the `gradient-motiond` executable target from `daemon/` sources and links it against the library and submodule targets.

**Alternatives considered**:
- Making all deps REQUIRED in Phase 0: blocks development on machines missing future-phase libraries. Unnecessary friction.
- Not checking future deps at all: loses the "clear error on missing dependency" benefit for developers setting up their environment early.

## 5. CLI Argument Parsing

**Decision**: Use `getopt_long` (POSIX, no external dependency) for `--midi-port`, `--log-level`, `--conf-path`, `--help`, `--version`.

**Rationale**: Minimal dependency footprint. VideoComposer uses a similar approach. CLI parsing libraries (boost::program_options, CLI11) are overkill for 3-4 flags.

**Arguments**:
| Long | Short | Type | Default | Description |
|------|-------|------|---------|-------------|
| `--midi-port` | `-m` | string | `"Midi Through Port-0"` | MIDI port name for MTC reception |
| `--log-level` | `-l` | string | `"info"` | Log verbosity: emergency, alert, critical, error, warning, notice, info, debug |
| `--conf-path` | `-c` | string | `"/etc/cuems"` | Path to CUEMS configuration directory |
| `--help` | `-h` | flag | — | Print usage and exit |
| `--version` | `-V` | flag | — | Print version and exit |

## 6. Signal Handling

**Decision**: Use `sigaction` to register handlers for SIGTERM and SIGINT that set `running_ = false`. Main loop uses `pause()` or a condition variable wait.

**Rationale**: Standard POSIX pattern for daemon processes. Matches systemd's `Type=simple` expectation (process runs in foreground, systemd sends SIGTERM on stop).

**Alternatives considered**:
- `signalfd` + `poll`: more robust for complex event loops but premature for Phase 0's simple blocking wait.
- `sigwait` in dedicated thread: useful when multiple threads need coordination, but Phase 0 is single-threaded.
