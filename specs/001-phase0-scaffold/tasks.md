# Tasks: Phase 0 — Project Scaffold

**Input**: Design documents from `/specs/001-phase0-scaffold/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: No test tasks — Phase 0 spec explicitly defers testing to Phase 1+.

**Organization**: Tasks grouped by user story from spec.md (US1–US4) to enable independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3, US4)
- Exact file paths included in descriptions

## Path Conventions

```text
src/                          ← libgradient_motion static library
  {time,motion,gradient,signal,osc,engine}/   ← gme::* namespace modules
daemon/                       ← gradient-motiond executable
  config/                     ← ConfigurationManager
cuemslogger/                  ← git submodule (optional via ENABLE_CUEMS_LOGGER)
mtcreceiver/                  ← git submodule (pinned to 63ce3de)
tests/                        ← empty in Phase 0
```

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create directory structure and initialize git submodules

- [ ] T001 Create `src/` module directories: `src/time/`, `src/motion/`, `src/gradient/`, `src/signal/`, `src/osc/`, `src/engine/` with one placeholder `.cpp` file per module (empty `gme::*` namespace declaration)
- [ ] T002 [P] Create `daemon/`, `daemon/config/`, and `tests/` directories (add `.gitkeep` to `tests/` so the empty directory is tracked by git)
- [ ] T003 Add `cuemslogger` git submodule from `https://github.com/stagesoft/cuemslogger.git` at repo root
- [ ] T004 [P] Add `mtcreceiver` git submodule from `https://github.com/stagesoft/mtcreceiver.git` at repo root, pinned to commit `63ce3de`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: CMake build system that all user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T005 Create root `CMakeLists.txt` — `cmake_minimum_required(VERSION 3.10)`, C++17 (`-Wall -O3 -pthread`), `pkg_check_modules(RTMIDI REQUIRED rtmidi)`, non-required checks for nng/tinyxml2/liblo, `find_package(nlohmann_json QUIET)`, submodule presence checks with `FATAL_ERROR`, `ENABLE_CUEMS_LOGGER` option (default ON) with conditional `add_subdirectory(cuemslogger)` and `-DHAVE_CUEMS_LOGGER` define, `add_subdirectory(mtcreceiver)`, `gradient-motiond` executable target linking `gradient_motion`, `cuemslogger` (when enabled), `mtcreceiver`, `${RTMIDI_LIBRARIES}`
- [ ] T006 Create `src/CMakeLists.txt` — collect all module placeholder sources into `gradient_motion` STATIC library target with `target_include_directories` for `src/`

**Checkpoint**: Build system skeleton ready — `cmake ..` succeeds (no source files to compile yet). User story implementation can now begin.

---

## Phase 3: User Story 1 — Build System Bootstraps Successfully (Priority: P1) MVP

**Goal**: Both targets (`libgradient_motion.a` and `gradient-motiond`) compile from a clean checkout. `--help` prints usage. Missing submodules produce a clear error.

**Independent Test**: Run `cmake .. && make -j$(nproc)` from a clean build directory. Verify `libgradient_motion.a` and `gradient-motiond` exist. Run `./gradient-motiond --help` and confirm usage output. Remove a submodule directory and verify `cmake ..` produces a clear `FATAL_ERROR`.

### Implementation for User Story 1

- [ ] T007 [P] [US1] Create `daemon/logging.h` — compile-time logging abstraction: when `HAVE_CUEMS_LOGGER` is defined, include `cuemslogger/cuemslogger.h` and wrap `CuemsLogger` methods (`LOG_INFO`, `LOG_ERROR`, `LOG_WARNING`, `LOG_DEBUG`, etc.); when not defined, provide `<iostream>` fallback macros writing to `std::cerr`/`std::cout`. Include Doxygen docstrings per Constitution Principle VI
- [ ] T008 [P] [US1] Create `daemon/config/ConfigurationManager.h` and `daemon/config/ConfigurationManager.cpp` — store `midiPort_`, `logLevel_`, `confPath_` with defaults from data-model.md (`"Midi Through Port-0"`, `"info"`, `"/etc/cuems"`). Implement `parseArgs(int argc, char** argv)` using `getopt_long` for `--midi-port/-m`, `--log-level/-l`, `--conf-path/-c`, `--help/-h`, `--version/-V`. Include Doxygen docstrings
- [ ] T009 [US1] Create `daemon/GradientEngineApplication.h` and `daemon/GradientEngineApplication.cpp` — class with `running_`, `initialized_`, `config_` (`unique_ptr<ConfigurationManager>`), `logger_` (`CuemsLogger*` when enabled, `nullptr` when disabled). Skeleton `initialize(int argc, char** argv)` that parses CLI args and returns `false` on `--help`/`--version`/error. Skeleton `run()` returning `int` exit code. Skeleton `shutdown()`. Include Doxygen docstrings (depends on T007, T008)
- [ ] T010 [US1] Create `daemon/main.cpp` — construct `GradientEngineApplication` on stack, call `initialize(argc, argv)`, if true call `run()`, return exit code. Follow VideoComposer lifecycle pattern (depends on T009)
- [ ] T011 [US1] Build verification: both targets compile and link cleanly, `./gradient-motiond --help` prints usage listing `--midi-port`, `--log-level`, `--conf-path`, `--help`, `--version`
- [ ] T012 [US1] Verify missing-submodule error messages: temporarily rename `mtcreceiver/` and run `cmake ..` — confirm `FATAL_ERROR` identifies the missing submodule. Repeat for `cuemslogger/` (when `ENABLE_CUEMS_LOGGER=ON`). This validates US1 acceptance scenario 4 (depends on T011)

**Checkpoint**: MVP complete — both build artifacts produced, `--help` works, missing-submodule errors verified. This is the minimum viable deliverable.

---

## Phase 4: User Story 2 — Application Lifecycle and Logging (Priority: P2)

**Goal**: Application follows structured lifecycle (initialize → run → shutdown), handles termination signals cleanly, initializes CuemsLogger, respects `--log-level`, and stores CLI argument values.

**Independent Test**: Start `./gradient-motiond --log-level debug --midi-port "Test Port" --conf-path /tmp`, verify startup log appears in journal (`journalctl -t "Cuems:GradientEngine"`), send `SIGTERM`, verify clean shutdown log and exit code 0.

### Implementation for User Story 2

- [ ] T013 [US2] Implement `GradientEngineApplication::initialize()` fully in `daemon/GradientEngineApplication.cpp` — after CLI parsing: instantiate `CuemsLogger("GradientEngine")` when `HAVE_CUEMS_LOGGER` defined (store pointer in `logger_`), log startup info (version, configured midi port, log level, conf path), set `initialized_ = true`. Return `false` with usage on parse error
- [ ] T014 [US2] Implement signal handling in `daemon/GradientEngineApplication.cpp` — use `sigaction` to register handlers for `SIGTERM` and `SIGINT` that set `running_ = false`. Use file-scope `volatile sig_atomic_t` flag bridged to `running_` (depends on T013)
- [ ] T015 [US2] Implement `GradientEngineApplication::run()` in `daemon/GradientEngineApplication.cpp` — set `running_ = true`, enter `pause()` loop (`while (running_) pause()`), call `shutdown()` on exit, return `0`. Implement `shutdown()` — log shutdown event, clean up config (depends on T014)
- [ ] T016 [US2] Implement log level validation in `daemon/config/ConfigurationManager.cpp` — validate `logLevel_` against allowed values: `emergency`, `alert`, `critical`, `error`, `warning`, `notice`, `info`, `debug`. Reject invalid values with error message and return failure from `parseArgs()`
- [ ] T017 [US2] Implement log level filtering in `daemon/logging.h` or `daemon/GradientEngineApplication.cpp` — store the parsed `logLevel_` from ConfigurationManager and gate log macro output so that messages below the configured level are suppressed. This avoids modifying the shared CuemsLogger submodule (depends on T016)

**Checkpoint**: Application lifecycle fully functional — starts, logs at correct verbosity, blocks, shuts down cleanly on signal.

---

## Phase 5: User Story 3 — MTC Receiver Integration (Priority: P3)

**Goal**: mtcreceiver submodule compiles and links as part of the build without errors or local patches, pinned to upstream commit `63ce3de` (RtMidi 5.x fix).

**Independent Test**: Build from clean checkout with submodules. Verify `mtcreceiver.cpp.o` is produced. Verify no compiler warnings from mtcreceiver sources. Verify `HAVE_CUEMS_LOGGER` is NOT defined for mtcreceiver target.

### Implementation for User Story 3

- [ ] T018 [US3] Verify mtcreceiver compiles and links against `librtmidi >= 5.0` at pinned commit `63ce3de` — confirm `RTMIDI_INCLUDE_DIRS` and `RTMIDI_LIBRARIES` are passed correctly from root CMakeLists.txt, no local patches applied, no compiler errors or warnings
- [ ] T019 [US3] Verify `HAVE_CUEMS_LOGGER` is NOT defined for the mtcreceiver target — mtcreceiver uses its built-in stub logger, avoiding the `cuems_errors.h` dependency. Confirm in root `CMakeLists.txt` that no `-DHAVE_CUEMS_LOGGER` is added to mtcreceiver compile definitions

**Checkpoint**: MTC receiver integration validated — compiles cleanly as git submodule dependency.

---

## Phase 6: User Story 4 — CuemsLogger Integration with Stdlib Fallback (Priority: P3)

**Goal**: `ENABLE_CUEMS_LOGGER=ON` (default) compiles and links CuemsLogger with journal output. `ENABLE_CUEMS_LOGGER=OFF` builds without the submodule using `<iostream>` fallback. Same logging API in both cases via `daemon/logging.h`.

**Independent Test**: Build with `-DENABLE_CUEMS_LOGGER=ON`, run and verify journal output. Build with `-DENABLE_CUEMS_LOGGER=OFF`, run and verify stderr output. Compare that both use identical macro calls in daemon code.

### Implementation for User Story 4

- [ ] T020 [US4] Verify `daemon/logging.h` provides identical API for both backends — `LOG_INFO(msg)`, `LOG_ERROR(msg)`, `LOG_WARNING(msg)`, `LOG_DEBUG(msg)` (and remaining syslog levels) dispatch to CuemsLogger methods when `HAVE_CUEMS_LOGGER` defined, or to `std::cerr`/`std::cout` with level prefix and timestamp when not defined
- [ ] T021 [US4] Verify `ENABLE_CUEMS_LOGGER=ON` build path — cuemslogger submodule compiles, `-DHAVE_CUEMS_LOGGER` is defined for daemon target, `CuemsLogger("GradientEngine")` instantiated in `initialize()`, log messages appear in journal with syslog identifier `Cuems:GradientEngine`
- [ ] T022 [US4] Verify `ENABLE_CUEMS_LOGGER=OFF` build path — build succeeds without cuemslogger submodule present, no `-DHAVE_CUEMS_LOGGER` defined, daemon logs to stderr via `<iostream>` fallback, cuemslogger submodule check in CMakeLists.txt is skipped when OFF

**Checkpoint**: Logging abstraction complete — both build paths verified, API identical.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final verification across all stories

- [ ] T023 [P] Verify all public classes (`GradientEngineApplication`, `ConfigurationManager`) have Doxygen docstrings per Constitution Principle VI (brief, params, long description, exceptions, examples)
- [ ] T024 [P] Verify `--version` outputs version string and exits cleanly
- [ ] T025 Run `quickstart.md` validation: full clone-build-run-verify sequence from `specs/001-phase0-scaffold/quickstart.md`. Include timing spot-checks: build completes in under 2 minutes (SC-001), startup under 1 second (SC-002), shutdown under 1 second after SIGTERM (SC-003)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion — BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Foundational — produces MVP
- **US2 (Phase 4)**: Depends on US1 (needs compiled skeleton to add behavior)
- **US3 (Phase 5)**: Depends on Foundational only — can run in parallel with US1/US2/US4
- **US4 (Phase 6)**: Depends on US1 (needs daemon/logging.h and build system) — can run in parallel with US2/US3
- **Polish (Phase 7)**: Depends on all user stories being complete

### User Story Dependencies

- **US1 (P1)**: Foundational → US1 (no cross-story dependencies). **MVP target.** Includes submodule error verification (T012) per acceptance scenario 4
- **US2 (P2)**: Foundational → US1 → US2 (needs compiled app skeleton from US1). Includes log level filtering (T017) so `--log-level` is not just validated but actually gates output
- **US3 (P3)**: Foundational → US3 (independent of US1/US2/US4 — mtcreceiver integration is a build-time concern)
- **US4 (P3)**: Foundational → US1 → US4 (needs daemon/logging.h from US1 to verify both paths)

### Within Each User Story

- Header files before implementation files
- Skeleton before full implementation
- Core implementation before verification
- [P] tasks within a story can run in parallel

### Parallel Opportunities

- T001 and T002 can run in parallel (different directories)
- T003 and T004 can run in parallel (independent submodules)
- T007 and T008 can run in parallel (different files, no dependencies)
- US3 can run in parallel with US1, US2, US4 (only needs Foundational phase)
- US2 and US4 can run in parallel after US1 completes
- All Phase 7 [P] tasks can run in parallel

---

## Parallel Example: User Story 1

```text
# Launch parallel tasks (T007 and T008 — different files):
Task: "Create daemon/logging.h compile-time logging abstraction"
Task: "Create daemon/config/ConfigurationManager.h/.cpp with CLI parsing"

# Then sequential (T009 depends on T007, T008):
Task: "Create daemon/GradientEngineApplication.h/.cpp with skeleton lifecycle"

# Then sequential (T010 depends on T009):
Task: "Create daemon/main.cpp entry point"

# Then verification (T011 depends on all above):
Task: "Build verification: both targets compile, --help works"

# Then submodule error verification (T012 depends on T011):
Task: "Verify missing-submodule FATAL_ERROR messages"
```

## Parallel Example: After Foundational

```text
# After Phase 2 completes, these can run in parallel:
Stream A: US1 (build system MVP + submodule errors) → US2 (lifecycle + log filtering) → US4 (logging fallback)
Stream B: US3 (mtcreceiver verification)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (directories + submodules)
2. Complete Phase 2: Foundational (CMakeLists.txt)
3. Complete Phase 3: User Story 1 (both targets compile, --help works)
4. **STOP and VALIDATE**: `cmake .. && make && ./gradient-motiond --help`
5. This is the minimum deliverable — a compiling project with working CLI

### Incremental Delivery

1. Setup + Foundational → Build system ready
2. Add US1 → Both targets compile → **MVP!**
3. Add US2 → Application lifecycle works → Start/signal/stop
4. Add US3 → MTC receiver verified → Dependency chain validated
5. Add US4 → Logging fallback verified → Standalone builds possible
6. Polish → Docstrings, error messages, quickstart validation

---

## Notes

- [P] tasks = different files, no dependencies on incomplete tasks
- [Story] label maps task to specific user story for traceability
- No test tasks in Phase 0 — testing deferred to Phase 1+ per spec. Constitution Principle "Development Workflow > Testing" mandates unit tests for new functionality; Phase 0 is scaffold-only (empty module placeholders, skeleton lifecycle) so the testing obligation begins with Phase 1 when evaluatable logic is introduced. A `tests/` directory with `.gitkeep` is created in T002 to establish the convention
- Constitution Principle VI (Documentation Standards) requires Doxygen docstrings on all public classes — enforced in T023
- mtcreceiver pinned to `63ce3de` — update to main branch commit after stagesoft/mtcreceiver#2 merges
- XML configuration parsing excluded from Phase 0 — CLI arguments only
- `GradientEngine*` naming for top-level classes; `Fade*` reserved for future sub-elements
- Constitution "Performance & Safety Standards > Build reproducibility" requires pinned compiler flags and dependency versions — `cmake_minimum_required(VERSION 3.10)` in T005, submodule pin in T004
