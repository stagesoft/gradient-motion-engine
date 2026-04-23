# Feature Specification: Phase 0 — Project Scaffold

**Feature Branch**: `001-phase0-scaffold`
**Created**: 2026-04-10
**Status**: Draft
**Input**: User description: "Based on the initial plan developed in dev/C++ Node-Level GradientEngine — Implementation Plan.md, adapt the structure laid out to implement Phase 0 into this repository. Ask for any missing libraries or required files so it can be added or referenced."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Build System Bootstraps Successfully (Priority: P1)

A developer clones the repository (with `--recurse-submodules`) and runs the build system. The project compiles two targets: a static library (`libgradient_motion`) and the main executable. Both targets compile cleanly with no errors or warnings on the supported toolchain.

**Why this priority**: Without a working build system, no subsequent phase (curves, MTC sync, NNG comms, engine) can begin. This is the foundation for all development.

**Independent Test**: Run the build from a clean checkout on a system with all dependencies installed. Both targets MUST produce artifacts (static library file and executable binary).

**Acceptance Scenarios**:

1. **Given** a clean checkout with submodules initialized and all build dependencies installed, **When** the developer runs the build, **Then** the static library target (`libgradient_motion`) compiles successfully and produces a library artifact
2. **Given** a clean checkout with submodules initialized and all build dependencies installed, **When** the developer runs the build, **Then** the executable target compiles and links successfully, producing a runnable binary
3. **Given** the executable is built, **When** the developer runs it with `--help`, **Then** it prints usage information listing available CLI options
4. **Given** a clean checkout without submodules initialized, **When** the developer runs the build, **Then** the build system produces a clear error indicating that submodules must be initialized

---

### User Story 2 - Application Lifecycle and Logging (Priority: P2)

A developer starts the application skeleton. The application initializes CuemsLogger for structured logging, follows a structured lifecycle (initialize, run, shutdown), and handles termination signals cleanly. Configuration parsing from the CUEMS settings file is deferred to a future phase — Phase 0 uses CLI arguments and defaults only.

**Why this priority**: The application lifecycle and logging are prerequisites for every runtime subsystem (MTC, NNG, fade evaluation). Without them, Phases 1-5 cannot integrate. CuemsLogger provides the structured logging used across all CUEMS services.

**Independent Test**: Start the application with CLI arguments. Verify it initializes CuemsLogger, logs its startup state, and remains running until signaled to stop.

**Acceptance Scenarios**:

1. **Given** the application is started, **When** it initializes, **Then** it sets up CuemsLogger and logs startup information to the journal
2. **Given** the application is running, **When** it receives a termination signal (SIGTERM, SIGINT), **Then** it shuts down cleanly, logging the shutdown event and releasing all resources
3. **Given** the application is started with `--log-level debug`, **When** it runs, **Then** log output respects the requested verbosity level
4. **Given** the application is started with `--midi-port` and `--conf-path` arguments, **When** it initializes, **Then** it accepts and stores those values for use by future subsystems

---

### User Story 3 - MTC Receiver Integration (Priority: P3)

A developer integrates the existing MTC receiver component into the new project as a git submodule. The receiver compiles as part of the build and links against the current MIDI library version. It serves as the timecode source for all subsequent fade evaluation work.

**Why this priority**: The MTC receiver is an external shared component. Integrating it early as a submodule validates that the dependency chain (MIDI library, ALSA) works, and that upstream changes can be tracked cleanly.

**Independent Test**: Build the project with the MTC receiver submodule initialized. Verify the receiver code compiles and links without errors against the current MIDI library version.

**Acceptance Scenarios**:

1. **Given** the MTC receiver submodule is initialized, **When** the build runs, **Then** the receiver code compiles without errors against the current MIDI library
2. **Given** the MIDI library version uses updated API constants, **When** the submodule is pinned to the upstream-fixed commit (`63ce3de`), **Then** the build MUST succeed without any local patches
3. **Given** the MTC receiver submodule is included in the project, **When** a developer checks out the repo, **Then** they can initialize it with standard git submodule commands

---

### User Story 4 - CuemsLogger Integration with Stdlib Fallback (Priority: P3)

A developer integrates the CuemsLogger component as a git submodule behind a build flag (`ENABLE_CUEMS_LOGGER`, default ON). When enabled, the application uses CuemsLogger for structured syslog output consistent with other CUEMS services. When disabled, the application falls back to standard library logging (`<iostream>`) with no submodule dependency — enabling standalone builds outside the CUEMS ecosystem.

**Why this priority**: All CUEMS services use CuemsLogger for consistent log formatting and journal integration. However, the library (`libgradient_motion`) and daemon should be buildable without it for development, testing, and potential reuse outside CUEMS.

**Independent Test**: Build the project with `ENABLE_CUEMS_LOGGER=ON` and verify CuemsLogger output. Then build with `ENABLE_CUEMS_LOGGER=OFF` and verify stdlib fallback produces equivalent log messages to stderr.

**Acceptance Scenarios**:

1. **Given** `ENABLE_CUEMS_LOGGER=ON` and the CuemsLogger submodule is initialized, **When** the build runs, **Then** the logger code compiles, links, and the application uses CuemsLogger for all log output
2. **Given** `ENABLE_CUEMS_LOGGER=OFF`, **When** the build runs, **Then** the build succeeds without the CuemsLogger submodule, and the application logs to stderr via standard library
3. **Given** `ENABLE_CUEMS_LOGGER=ON` and the application runs under systemd, **When** it logs messages, **Then** they appear in the system journal with the correct syslog identifier
4. **Given** either logging backend, **When** the developer uses the logging interface, **Then** the API is identical (same function calls) regardless of backend — the selection is compile-time only

---

### Edge Cases

- What happens when required git submodules are not initialized (submodule directory absent or empty)? The build system MUST produce a clear error message identifying which submodule is missing (except cuemslogger when `ENABLE_CUEMS_LOGGER=OFF`).
- What happens when `ENABLE_CUEMS_LOGGER=ON` but the cuemslogger submodule is not initialized? The build MUST fail with a clear error message.
- What happens when a CLI argument is invalid or unrecognized? The application MUST print a usage message and exit with a non-zero status code.
- What happens when required build dependencies are missing? The build system MUST produce a clear error message identifying the missing dependency.
- What happens when the MIDI library is too old for the MTC receiver? The build MUST fail with a message indicating the minimum required version.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The build system MUST define two targets: a static library (`libgradient_motion`) organized by `gme::*` namespace modules, and a daemon executable (`gradient-motiond`)
- **FR-002**: The build system MUST enforce the C++17 standard with warnings enabled and optimizations for release builds
- **FR-003**: The executable MUST accept CLI arguments for `--midi-port` (MIDI port name), `--log-level` (logging verbosity override), `--conf-path` (configuration directory path), `--help` (print usage and exit), and `--version` (print version and exit)
- **FR-004**: The application MUST follow a structured lifecycle pattern: initialize, run (block until signal), shutdown (release resources)
- **FR-005**: The application MUST handle POSIX termination signals (SIGTERM, SIGINT) for clean shutdown
- **FR-006**: The build system MUST integrate the MTC receiver as a git submodule, compiling it as a subdirectory target
- **FR-007**: The build system MUST provide an `ENABLE_CUEMS_LOGGER` option (default ON) that controls whether CuemsLogger is compiled and linked
- **FR-008**: When `ENABLE_CUEMS_LOGGER=ON`, the application MUST use CuemsLogger for all log output with journal integration. When OFF, the application MUST fall back to standard library logging (`<iostream>` to stderr)
- **FR-009**: The daemon MUST expose a compile-time logging abstraction (`daemon/logging.h`) that provides a uniform API regardless of the selected backend
- **FR-010**: The build system MUST declare all external library dependencies so that missing dependencies produce clear build-time errors
- **FR-011**: Third-party and shared CUEMS components MUST be included as git submodules, not as copied source files, to enable upstream tracking

### Key Entities

- **GradientEngineApplication**: The top-level daemon application object managing lifecycle (initialize, run, shutdown) and signal handling. Named after the engine, not the fade sub-component — designed to orchestrate multiple signal output types in the future
- **ConfigurationManager**: Skeleton responsible for storing CLI-provided values; XML settings file parsing is deferred to a future phase
- **MtcReceiver**: Shared CUEMS submodule that decodes MIDI timecode quarter-frame messages into a millisecond position (compilation target in Phase 0, active use in Phase 2)
- **CuemsLogger**: Shared CUEMS submodule providing structured logging with journal integration

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A developer can build both targets from a clean checkout (with submodules) in under 2 minutes on standard hardware
- **SC-002**: The executable starts and reaches a ready state in under 1 second
- **SC-003**: The executable shuts down cleanly within 1 second of receiving a termination signal
- **SC-004**: With `ENABLE_CUEMS_LOGGER=ON`, all log output uses CuemsLogger and appears correctly in the system journal
- **SC-005**: With `ENABLE_CUEMS_LOGGER=OFF`, the project builds and runs successfully using stdlib fallback logging
- **SC-006**: The MTC receiver submodule compiles and links without errors or warnings on the supported toolchain
- **SC-007**: The CuemsLogger submodule compiles and links without errors or warnings when enabled

## Clarifications

### Session 2026-04-10

- Q: How should RtMidi 5.x API compatibility be handled for the mtcreceiver submodule? → A: Fixed upstream (Option B). PR stagesoft/mtcreceiver#2, commit `63ce3de`. Pin submodule to this commit until PR is merged to main, then update to the merged commit.

## Assumptions

- The target platform is Linux (Debian-based) with systemd as the service manager
- All build dependencies (C++17 compiler, CMake, libnng-dev, nlohmann-json3-dev, libtinyxml2-dev, liblo-dev, librtmidi-dev >= 5.0, libasound2-dev) are available in the target distribution's package manager
- The MTC receiver is available as a git submodule from `https://github.com/stagesoft/mtcreceiver.git`, pinned to commit `63ce3de` (RtMidi 5.x fix from stagesoft/mtcreceiver#2) until the PR is merged to main
- The CuemsLogger is available as a git submodule from `https://github.com/stagesoft/cuemslogger.git`
- XSD schema changes and XML configuration parsing are excluded from Phase 0; these integrations will be implemented in future phases
- The systemd service file is part of Phase 5, not Phase 0
- The `libgradient_motion` library target is created with the `gme::*` module directory structure (time, motion, gradient, signal, osc, engine) as empty placeholders in Phase 0; actual implementations begin in Phase 1
- Configuration values from the settings XML file are not parsed in Phase 0; only CLI arguments provide runtime configuration
- When XML configuration parsing is implemented in a future phase, the path to the settings file MUST be passable as a CLI argument (e.g., `--conf-path` already accepted in Phase 0)
