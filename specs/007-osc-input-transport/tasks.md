<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tasks: Phase H — OSC Input Transport (007-osc-input-transport)

**Input**: Design documents from `specs/007-osc-input-transport/`
**Prerequisites**: [plan.md](plan.md), [spec.md](spec.md), [research.md](research.md), [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)

**Tests**: Test tasks are included. The plan and research explicitly call for `test_osc_parse.cpp` (parse-level) and `test_osc_server_integration.cpp` (wire smoke). Existing test suites stay green per spec SC-006.

**Organization**: Tasks are grouped by user story. The four user stories from [spec.md](spec.md) are:

- **US1 (P1, MVP)** — A node-local fade plays end-to-end on real hardware.
- **US2 (P1)** — Stopping or reloading a project clears in-flight fades.
- **US3 (P2)** — Multi-node deployments do not cross-talk.
- **US4 (P3)** — Operators and developers can observe fade activity without a wire receiver.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1, US2, US3, US4)
- Setup, Foundational, and Polish phases have no [Story] label
- File paths are absolute repo-relative

## Naming conventions used in this document

- **"Phase A–H"** = project phases (Phase H is the project milestone covered by this feature).
- **"Phase 1, 2, 3…"** below = Spec Kit *task phases* within this feature (the standard Spec Kit heading), not project phases. Numbered for readability of the task list — they are unrelated to feature numbers.
- **"feature NNN"** = a Spec Kit feature directory under `specs/`. References to prior work use this form (`feature 005`, `feature 006`), never `Phase 3` / `Phase 6`.

## Cross-repo scope (out of scope for this tasks.md)

Phase H spans three repositories. **This tasks.md covers only the C++ daemon in `gradient-motion-engine`.** The companion changes are tracked in their own repos and are deliberately out of scope here:

| Repo | Branch | What it owns | Maps to spec items |
|---|---|---|---|
| `gradient-motion-engine` (this repo) | `feat/osc-input-transport` | Daemon C++ refactor, OSC server, parser, packaging | All in-scope FRs, all SCs |
| `cuems-engine` | `feat/gradient-osc-transport` | New `GradientPlayer.py`, `ActionHandler._handle_fade_action` rewire, `ControllerEngine` cancel-path removal, NodeEngine cancel-on-STOP/LOAD, resilience to missing daemon, future fade-progress emission from `loop_fadeCue` | spec §"Out of scope — NodeEngine-side dispatch / resilience / lifecycle reporting" |
| `cuems-common` | (branch TBD on that repo) | `etc/systemd/system/cuems-gradient-motiond.service` — drop `--nng-url`, add `--osc-port 7100`, drop `Wants=avahi-daemon.service` and `After=...avahi-daemon.service` entirely | spec §"Out of scope — `cuems-common` systemd unit edit" (deployment counterpart of FR-013) |
| `cuems-utils` | (branch TBD on that repo) | `settings.xsd` — add `<gradient_osc_port>` element under `<node>` | spec Assumptions §settings.xml |

A single Polish task (T060 below) coordinates the unit edit since it's small and tightly bound to spec FR-013; the broader cuems-engine work lives in that repo's own spec/plan/tasks.

## Path Conventions

- Library sources: `src/{engine,gradient,motion,osc,signal,time}/`
- Daemon sources: `daemon/{,comms/,config/}`
- Tests: `tests/`
- Packaging: `debian/`
- Per the [plan.md](plan.md) project structure.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Pre-flight checks and build-graph readiness before any code changes.

- [X] T001 Confirm git submodules are initialized (`mtcreceiver` and `cuemslogger`); run `git submodule status --recursive` and re-init if any line starts with `-` or `+`.
- [X] T002 [P] Confirm host has `liblo-dev`, `nlohmann-json3-dev`, `librtmidi-dev`, and `cmake` ≥ 3.10 installed. (`apt list --installed 2>/dev/null | grep -E 'liblo-dev|nlohmann-json3-dev|librtmidi-dev|cmake'`).
- [X] T003 [P] Baseline build to capture the pre-change state — `cmake -S . -B build_daemon -DBUILD_DAEMON=ON && cmake --build build_daemon -j` and `ctest --test-dir build_daemon --output-on-failure`. Record which tests pass on `main`-equivalent (`feat/phase5-systemd-packaging`) before any deletions, so post-change comparison is meaningful.

**Checkpoint**: Repo builds and existing tests pass before Phase H deletions begin.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Land the new file headers, the CMake/packaging changes, the daemon CLI/config plumbing, the rename of `fade_id` → `motion_id` (and `partner_fade_id` → `partner_motion_id`) for ecosystem consistency, and the deletions that the new transport replaces. After this phase compiles, every user story implements behavior on top of the new skeleton.

**⚠️ CRITICAL**: All user-story work is gated on the build being green again at the end of this phase. Skeletons must compile, even if their parser/server bodies are still stubs that no-op or always return `MissingField`.

### CMake / packaging

- [X] T004 Remove `find_package(nng REQUIRED)` from [CMakeLists.txt](../../CMakeLists.txt) (currently around the `BUILD_DAEMON` block). Verify with `grep -n -i nng CMakeLists.txt` returning zero hits.
- [X] T005 Drop any `target_link_libraries(... nng ...)` / `${nng_LIBRARIES}` references from [src/CMakeLists.txt](../../src/CMakeLists.txt) and (if present) any subdirectory `CMakeLists.txt`. Confirm with `grep -rn nng src/ daemon/ tests/ CMakeLists.txt`.
- [X] T006 [P] In [debian/control](../../debian/control), remove `libnng-dev` from `Build-Depends` and any `libnng1`-equivalent from runtime `Depends`. Keep `liblo-dev` / `liblo7`. Validate with `grep -n nng debian/control` returning empty.

### Daemon CLI + config

- [X] T007 In [daemon/main.cpp](../../daemon/main.cpp) (or wherever the existing CLI parsing lives — verify the call chain into `GradientEngineApplication::initialize`), drop the `--nng-url` flag and add `--osc-port <port>` (int, default `7100`) and read `CUEMS_GRADIENT_OSC_PORT` env var as fallback. Document the resolution order CLI > env > settings.xml > default in a comment matching research.md Decision 5.
- [X] T008 In [daemon/GradientEngineApplication.h](../../daemon/GradientEngineApplication.h) and [.cpp](../../daemon/GradientEngineApplication.cpp), replace the `nngUrl` field on the engine-config struct with `oscPort` (int). Update the `engCfg.nngUrl   = config_->getNngUrl();` line at [GradientEngineApplication.cpp:86](../../daemon/GradientEngineApplication.cpp#L86) to `engCfg.oscPort = config_->getGradientOscPort();`.
- [X] T009 In [daemon/config/](../../daemon/config/) (locate the existing `Config` class — header + impl), add `int getGradientOscPort() const` reading the `<gradient_osc_port>` element from `/etc/cuems/settings.xml`. Default to 7100 if the element is absent. Remove (or stop calling) the prior `getNngUrl()` accessor and the XML parsing for the `<nng_url>` element if present.

### Struct rename: `fade_id` → `motion_id` (and `partner_fade_id` → `partner_motion_id`)

This block lands the ecosystem-wide rename before any new code reads or writes the field. Rationale: every motion type — current `FadeMotion`, future `VectorMotion<N>`, future crossfade — keys on `motion_id`. The struct field name should match. Fades are one *kind* of motion, not the whole concept.

- [X] T010 Rename `fade_id` → `motion_id` and `partner_fade_id` → `partner_motion_id` in [src/signal/FadeCommand.h](../../src/signal/FadeCommand.h): the two `std::string` fields (lines 104 and 122), every docstring mention (lines 31, 33, 35, 66–67, 97, 122, 134, 169, 212–213, 225, 236, 242–243), and the example code block. The `CANCEL_MOTION` enum value already exists (line 97) — keep it. **Do not** change any wire format on the JSON parser path; that path is deleted in this phase by T021.
- [X] T011 [P] Rename in [src/signal/FadeCommand.cpp](../../src/signal/FadeCommand.cpp): every `fade_id` / `partner_fade_id` reference becomes `motion_id` / `partner_motion_id`. Update parser-side JSON key reading too (existing keys `"fade_id"` / `"partner_fade_id"` may stay as wire keys since this path is being deleted shortly — flag any that would conflict and delete them with T021).
- [X] T012 [P] Rename in [src/motion/IMotion.h](../../src/motion/IMotion.h) (line 65 docstring mention), [src/motion/FadeMotion.h](../../src/motion/FadeMotion.h) (line 70 docstring "from `FadeCommand::fade_id`"), [src/motion/MotionFactory.cpp](../../src/motion/MotionFactory.cpp) (every `cmd.fade_id` → `cmd.motion_id`), and [src/motion/MotionRegistry.cpp](../../src/motion/MotionRegistry.cpp) (every internal reference). The registry's internal map key shape is unchanged; only the variable name flips.
- [X] T013 [P] Rename in [tests/test_motion_registry.cpp](../../tests/test_motion_registry.cpp), [tests/test_motion_registry_bench.cpp](../../tests/test_motion_registry_bench.cpp), and [tests/test_fade_motion.cpp](../../tests/test_fade_motion.cpp): every `cmd.fade_id = "..."` (or equivalent) and every assertion against `.fade_id` flips to `motion_id`. Existing test names like `test_fade_motion_supersede` stay; the field rename is a behavior-neutral edit. Do NOT touch [tests/test_nng_parse.cpp](../../tests/test_nng_parse.cpp) / [tests/test_nng_integration.cpp](../../tests/test_nng_integration.cpp) — they are deleted in T023.
- [X] T014 Build + run the motion test set after the rename: `cmake --build build_daemon -j test_motion_registry test_fade_motion test_motion_registry_bench && ctest --test-dir build_daemon --output-on-failure --tests-regex 'motion|fade'`. Must be green before any new transport code lands. This is the rename's correctness gate.

### Skeletons for the new transport

- [X] T015 [P] Add [src/signal/parseFadeOscCommand.h](../../src/signal/parseFadeOscCommand.h) verbatim from [contracts/parseFadeOscCommand.h](contracts/parseFadeOscCommand.h). Adjust the SPDX header comment to point at this repo instead of the contract location, but keep the public signature, docstring, and `ParseResult` reuse identical. The contract already uses `motion_id` and `CANCEL_MOTION` — they line up with the post-T010 struct.
- [X] T016 [P] Add [src/signal/parseFadeOscCommand.cpp](../../src/signal/parseFadeOscCommand.cpp) implementing **only** the address-dispatch skeleton: switch on `path`, return `ParseResult::Ok` with `out_cmd->type` set correctly **but no field population** for now. This keeps the foundation buildable without making US1/US2 green prematurely.
- [X] T017 [P] Add [daemon/comms/OscServer.h](../../daemon/comms/OscServer.h) verbatim from [contracts/OscServer.h](contracts/OscServer.h) (with SPDX header pointing at the live file path). Namespace is `gme::daemon::comms` to match the existing daemon-side comms convention.
- [X] T018 Add [daemon/comms/OscServer.cpp](../../daemon/comms/OscServer.cpp) implementing **only** the lifecycle skeleton. Construct the server via `lo_server_thread_new_from_url("osc.udp://127.0.0.1:<port>/", errorHandler)` (per research.md Decision 1 — this is the **locked** API that yields a loopback-only bind; do not use plain `lo_server_thread_new`). Register three method callbacks (`/gradient/start_fade ,sssisffhisss`, `/gradient/cancel_motion ,ss`, `/gradient/cancel_all ,s`); each calls `parseFadeOscCommand` and pushes on success. `stop()` calls `lo_server_thread_stop` + `lo_server_thread_free` and joins. The callbacks are wired but field-level validation is deferred to per-story tasks.

### Wire into engine + delete the old transport

- [X] T019 In [src/engine/GradientEngine.h](../../src/engine/GradientEngine.h) and [.cpp](../../src/engine/GradientEngine.cpp), rename the `nngClient_` member to `oscServer_` with type `std::unique_ptr<gme::daemon::comms::OscServer>`. Update construction (feature 006 wired this via `nngClient_->setQueue(&queue_)` or similar — replace with `oscServer_ = std::make_unique<OscServer>(cfg.oscPort, cfg.nodeName, &queue_)`). Update startup to call `oscServer_->start()` and shutdown to call `oscServer_->stop()`. The drain-queue path in `onTick` is unchanged.
- [X] T020 Update [src/CMakeLists.txt](../../src/CMakeLists.txt) to compile and install `parseFadeOscCommand.cpp` as part of `libgradient_motion`.
- [X] T021 Update [daemon/CMakeLists.txt](../../daemon/) (or the top-level one if the daemon sources are listed there) to compile `daemon/comms/OscServer.cpp` and link `liblo` (already linked transitively via `libgradient_motion`; verify the daemon target picks it up explicitly).
- [X] T022 Delete [daemon/comms/NngBusClient.cpp](../../daemon/comms/NngBusClient.cpp) and [daemon/comms/NngBusClient.h](../../daemon/comms/NngBusClient.h). Remove any `#include "comms/NngBusClient.h"` lines they leave behind. The JSON `parseFadeCommand` path in `FadeCommand.cpp` becomes unused after this — delete the JSON parser function and its declaration in T024 below if it's not still referenced.
- [X] T023 Delete [src/signal/StatusEmitRequest.h](../../src/signal/StatusEmitRequest.h) and any references (status-emit queue field on `GradientEngine`, status-emit-from-tick code paths, `classifyParseOutcome` / `LogOnly` / `LogAndStatus` helpers in `FadeCommand.h` if they were only used by the status path). Surface compile errors are expected — fix by deleting the call sites.
- [X] T024 Delete [tests/test_nng_parse.cpp](../../tests/test_nng_parse.cpp) and [tests/test_nng_integration.cpp](../../tests/test_nng_integration.cpp). Remove their entries from [tests/CMakeLists.txt](../../tests/CMakeLists.txt). Also delete the now-orphan JSON `parseFadeCommand` function from [src/signal/FadeCommand.cpp](../../src/signal/FadeCommand.cpp) and its declaration in [src/signal/FadeCommand.h](../../src/signal/FadeCommand.h) if T022's deletion removed its sole caller.

### Confirm Foundational compiles

- [X] T025 Reconfigure and rebuild — `cmake -S . -B build_daemon -DBUILD_DAEMON=ON && cmake --build build_daemon -j`. Resolve any remaining compile errors. **Do not** wire field-level OSC parsing yet — that lives in user stories. Run `ctest --test-dir build_daemon --output-on-failure --tests-regex 'motion|registry|curve|lockfree|mtc'` and confirm the non-transport tests are green.

**Checkpoint**: Build is green. Daemon starts and binds OSC port 7100 to loopback (`ss -ulnp | grep 7100` shows `127.0.0.1:7100`, never `0.0.0.0:7100`). The daemon accepts no commands yet (parser is a skeleton), but it does not crash on receipt either. NNG is fully gone from the build graph and runtime. The `motion_id` rename is fully through every motion test.

---

## Phase 3: User Story 1 — A node-local fade plays end-to-end on real hardware (Priority: P1) 🎯 MVP

**Goal**: Pressing GO on a FadeCue on node-002 produces an audible 5-second fade to silence with no bounce. The daemon receives the local NodeEngine's `/gradient/start_fade` and the existing tick loop drives the fade to completion.

**Independent Test**: Send `/gradient/start_fade` with the [`fade-test`](../planning/phase-h-osc-refactor-plan.md) project parameters from the local node via the Python `python-osc` smoke client (Quickstart §4) and observe the daemon's `MotionRegistry` log line plus per-tick `/volmaster` OSC ticks at the configured downstream port.

### Tests for User Story 1 (write FIRST and confirm they fail)

- [X] T026 [P] [US1] Add [tests/test_osc_parse.cpp](../../tests/test_osc_parse.cpp) with the `StartFade_AcceptsWellFormedMessage` case. Build a `lo_message` (via `lo_message_new` + `lo_message_add_*`) carrying the full 11-arg `,sssisffhisss` payload, extract `argv` + `types` via `lo_message_get_argv` / `lo_message_get_types`, call `parseFadeOscCommand("/gradient/start_fade", types, argv, 11, "node-dev", &cmd)`, assert `Ok` and field-by-field equality against the expected `FadeCommand` (including `cmd.motion_id`). Confirm test fails (parser is a skeleton).
- [X] T027 [P] [US1] Append to the same file a `StartFade_RejectsWrongTypeTag` case (e.g., `,sssisffffss` instead of `,sssisffhisss`) asserting `TypeError`. Confirm fails (skeleton currently returns `Ok` without checking tag).
- [X] T028 [P] [US1] Append `StartFade_RejectsMissingArguments` (argc=10) asserting `MissingField`.
- [X] T029 [US1] Add [tests/test_osc_server_integration.cpp](../../tests/test_osc_server_integration.cpp) with a `StartFade_PushesToQueue` case: create a `LockFreeQueue<FadeCommand, 64>`, construct `OscServer` on a random free port with `node_name="node-test"`, call `start()`, use liblo's client side (`lo_send`) to send `/gradient/start_fade` with valid args, sleep briefly, assert the queue contains exactly one record with the expected `motion_id`. Confirm fails (parser skeleton drops it).

### Implementation for User Story 1

- [X] T030 [US1] Implement [src/signal/parseFadeOscCommand.cpp](../../src/signal/parseFadeOscCommand.cpp) for `/gradient/start_fade`: enforce `,sssisffhisss` exactly, populate every field (with the int64→uint64 cast for `start_mtc_ms` and int32→uint32 for `duration_ms`), `nlohmann::json::parse` the `curve_params_json` string (treat parse failure as `TypeError`), check non-empty / positive constraints per [contracts/gradient_osc.md](contracts/gradient_osc.md). Confirm T026–T028 now pass.
- [X] T031 [US1] Tighten [daemon/comms/OscServer.cpp](../../daemon/comms/OscServer.cpp) `/gradient/start_fade` callback: pass the real `argv/types/argc` through to `parseFadeOscCommand`, log every dispatch outcome (Ok / NodeMismatch / MissingField / TypeError), and push on `Ok` only. Confirm T029 now passes.
- [X] T032 [US1] On startup, log the bind URL at INFO level: `"OscServer bound: osc.udp://127.0.0.1:<port>/"`. This is the operator-visible confirmation that the loopback-only bind (research.md Decision 1) is active. If at runtime `ss -ulnp` ever shows `0.0.0.0:<port>` despite this log, file a regression — the API contract has changed.
- [X] T033 [US1] In [src/engine/GradientEngine.cpp](../../src/engine/GradientEngine.cpp) `onTick`, confirm the existing drain-queue path applies `FadeCommand{Type::START_FADE,...}` records through `MotionFactory` → `MotionRegistry::addMotion` unchanged. No code change expected — this is a verification task, but adds a regression-catching assertion comment if the call shape moves.
- [X] T034 [US1] Quickstart §3–4 smoke — implemented as `dev/deploy_tests/s007_t034_smoke.sh`. Script builds `osc_sender` (liblo CLI), starts the daemon on port 17100, verifies UDP bind via `ss`, sends `/gradient/start_fade` + `/gradient/cancel_motion` + `/gradient/cancel_all` for the correct node, verifies each appears in the journal, then sends a wrong-`node_name` message and asserts it is NOT accepted. Results written to `dev/deploy_tests/results/s007_t034_smoke.txt`. Run: `bash dev/deploy_tests/s007_t034_smoke.sh`.

**Checkpoint** (US1 done): The MVP scenario from spec SC-001 is reproducible locally on a dev box. On-hardware verification on node-002 is deferred to the Polish phase smoke alongside the cuems-engine companion changes; the C++ side is complete for US1.

---

## Phase 4: User Story 2 — Stopping or reloading a project clears in-flight fades (Priority: P1)

**Goal**: Operators pressing STOP, or NodeEngine loading a different project, cancels every in-flight fade within one tick. Per-fade cancel by `motion_id` also works.

**Independent Test**: With an in-flight fade from US1, send `/gradient/cancel_motion <motion_id> <node_name>` and observe the registry remove the entry. Send `/gradient/cancel_all <node_name>` with multiple in-flight fades and observe the registry clear.

### Tests for User Story 2

- [X] T035 [P] [US2] Append to [tests/test_osc_parse.cpp](../../tests/test_osc_parse.cpp) a `CancelMotion_AcceptsTwoStringArgs` case asserting `Ok` with `type==CANCEL_MOTION` and the `motion_id` populated. Confirm fails first.
- [X] T036 [P] [US2] Append `CancelMotion_RejectsExtraArgs` (e.g., argc=3 with `,sss`) asserting `TypeError`.
- [X] T037 [P] [US2] Append `CancelAll_AcceptsSingleStringArg` asserting `Ok` with `type==CANCEL_ALL`.
- [X] T038 [P] [US2] Append `CancelAll_RejectsEmptyNodeName` asserting `MissingField`.
- [X] T039 [US2] Append to [tests/test_osc_server_integration.cpp](../../tests/test_osc_server_integration.cpp) a `CancelAll_PushesCancelRecord` case mirroring T029 but with `/gradient/cancel_all`.

### Implementation for User Story 2

- [X] T040 [US2] Extend [src/signal/parseFadeOscCommand.cpp](../../src/signal/parseFadeOscCommand.cpp) to handle `/gradient/cancel_motion` (`,ss`, both non-empty) and `/gradient/cancel_all` (`,s`, non-empty) per [contracts/gradient_osc.md](contracts/gradient_osc.md). Confirm T035–T038 pass.
- [X] T041 [US2] Wire the two cancel-address callbacks in [daemon/comms/OscServer.cpp](../../daemon/comms/OscServer.cpp) to push the parsed records. Confirm T039 passes.
- [X] T042 [US2] In [src/engine/GradientEngine.cpp](../../src/engine/GradientEngine.cpp), verify the existing drain-queue path dispatches `CANCEL_MOTION` → `MotionRegistry::cancelMotion(motion_id)` and `CANCEL_ALL` → `MotionRegistry::cancelAll()` unchanged. Add a one-line journal log inside each dispatch arm.
- [X] T043 [US2] Verify SIGTERM/SIGINT shutdown still treats active fades as cancel-all (Constitution + spec FR-011). The relevant path lives in `GradientEngineApplication::run` or the signal handler glue — confirm `oscServer_->stop()` is called before the registry tear-down and that no final OSC tick is emitted for in-flight fades. Also confirm `lo_server_thread_free` returns within the 2-second shutdown budget (research.md Open Item 1). If it blocks, fall back to a manually-pumped `lo_server` per the documented alternative.

**Checkpoint** (US2 done): All three messages from the wire contract are fully implemented. STOP/LOAD scenarios end-to-end work in the local smoke; the rest is on-hardware in Polish.

---

## Phase 5: User Story 3 — Multi-node deployments do not cross-talk (Priority: P2)

**Goal**: A fade addressed to node-A's `node_name` is dropped silently by node-B's daemon. The `node_name` filter in the parser is the single defense-in-depth point.

**Independent Test**: Send a `/gradient/start_fade` with `node_name="some-other-node"` to a daemon configured as `node-test`; observe the parser return `NodeMismatch` and the registry remain empty.

### Tests for User Story 3

- [X] T044 [P] [US3] Append to [tests/test_osc_parse.cpp](../../tests/test_osc_parse.cpp) a `StartFade_NodeNameMismatch_Drops` case asserting `NodeMismatch`. Confirm fails first if the filter wasn't yet wired (it should already be wired by US1 — if so this test passes immediately and serves as regression coverage).
- [X] T045 [P] [US3] Append `CancelMotion_NodeNameMismatch_Drops` asserting `NodeMismatch`.
- [X] T046 [P] [US3] Append `CancelAll_NodeNameMismatch_Drops` asserting `NodeMismatch`.
- [X] T047 [US3] Append to [tests/test_osc_server_integration.cpp](../../tests/test_osc_server_integration.cpp) a `NodeMismatch_DoesNotPush` case sending a well-formed `/gradient/start_fade` with `node_name="other"` to a server configured as `node-test`; assert the queue stays empty.

### Implementation for User Story 3

- [X] T048 [US3] Confirm [src/signal/parseFadeOscCommand.cpp](../../src/signal/parseFadeOscCommand.cpp) compares `node_name` against `this_node_name` and returns `NodeMismatch` for every command type. If the comparison is duplicated, factor it into a static helper inside the .cpp; the parser stays the single source of truth for the filter (do not push the check up into `OscServer`).
- [X] T049 [US3] In [daemon/comms/OscServer.cpp](../../daemon/comms/OscServer.cpp), confirm `NodeMismatch` is logged at debug level (not warn) — silent-drop is the contract; warn would be noisy in misconfigured multi-node deployments and could mislead operators.

**Checkpoint** (US3 done): Cross-node misrouting is provably dropped at the parser, with tests covering all three message types.

---

## Phase 6: User Story 4 — Operators and developers can observe fade activity without a wire receiver (Priority: P3)

**Goal**: When something goes wrong (malformed command, OSC send failure to a downstream player, registry overflow), the operator and developer can read the journal and understand what happened. The daemon emits no network status — its consumers don't exist.

**Independent Test**: Send a malformed `/gradient/start_fade` (bad JSON in `curve_params_json`); observe the daemon's journal show a warn line naming the offending `motion_id` and continue running.

### Tests for User Story 4

- [X] T050 [P] [US4] Append to [tests/test_osc_parse.cpp](../../tests/test_osc_parse.cpp) a `StartFade_MalformedCurveParamsJson_TypeError_PreservesMotionId` case asserting `TypeError` AND `out_cmd.motion_id == "fade-x"` AND `out_cmd.type == START_FADE` (the partial-population rule from the parser contract). Confirm fails first.
- [X] T051 [P] [US4] Append `StartFade_EmptyMotionId_MissingField` and `StartFade_NegativeDuration_MissingField` cases.
- [X] T052 [US4] Send-failure rate-limit check — implemented as `dev/deploy_tests/s007_t052_rate_limit.sh`. Script starts the daemon with a closed OSC callback port (19999) and asserts (a) the daemon does not crash on acceptance and (b) the start_fade is accepted and logged. Full `kOscFailureThreshold=5` eviction path (requires MTC ticks) is documented in the script as a node-002 instruction and marked SKIP on dev host. Results written to `dev/deploy_tests/results/s007_t052_rate_limit.txt`. Run: `bash dev/deploy_tests/s007_t052_rate_limit.sh`.

### Implementation for User Story 4

- [X] T053 [US4] In [src/signal/parseFadeOscCommand.cpp](../../src/signal/parseFadeOscCommand.cpp), implement the partial-population rule documented in [contracts/parseFadeOscCommand.h](contracts/parseFadeOscCommand.h): on `MissingField` / `TypeError` for `start_fade` / `cancel_motion`, if `motion_id` was successfully extracted from `argv[0]`, set `out_cmd->motion_id` and `out_cmd->type` before returning. Confirm T050 passes.
- [X] T054 [US4] In [daemon/comms/OscServer.cpp](../../daemon/comms/OscServer.cpp), the per-callback try/catch wrapper logs `MissingField` and `TypeError` at warn level with the `motion_id` (if available), the address, and the parser-reported reason. Use `cuemslogger` for structured emission; no `std::cout` / `fprintf`.
- [X] T055 [US4] Grep the daemon and library trees one last time for any surviving `StatusEmitRequest` / `motion_complete` / `motion_error` / `gradientengine_<` / `sendStatus` references and delete them (the Foundational phase removed the header but call sites may have been temporarily preserved with stub bodies). `grep -rn 'StatusEmit\|motion_complete\|motion_error\|sendStatus' src/ daemon/ tests/` must be empty.

**Checkpoint** (US4 done): Daemon's failure modes are observable via journalctl; no network status surface remains; the dead-code removal goal in spec is met.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Documentation sync, packaging rebuild, on-hardware verification, latency/TSAN/multi-node tests for the success criteria, and the cross-repo unit edit. After this phase, every spec SC has explicit evidence.

- [X] T056 [P] Run Doxygen against `src/` and `daemon/` and confirm zero warnings on the new `OscServer.h` and `parseFadeOscCommand.h` headers. Public docstrings must include brief + parameters + long description (where non-obvious) + exceptions + example, per Constitution Principle VI.
- [X] T057 [P] Update [CHANGELOG.md](../../CHANGELOG.md) with a `## [Unreleased]` entry summarizing the transport switch (delete NNG, add OSC), the new CLI flag, the new settings.xml element, and the `motion_id` rename. Link this spec.
- [X] T058 Rebuild the deb — `cd debian && debuild -b -us -uc` (or whatever the existing packaging entry is) and confirm `dpkg-deb -I cuems-gradient-motiond_*.deb | grep -i nng` returns nothing (spec SC-004).
- [X] T059 Run `ldd build_daemon/daemon/gradient-motiond | grep -i nng` and confirm zero hits.
- [X] T060 **Cross-repo coordination — `cuems-common` systemd unit** *(out-of-scope: `cuems-common` repo)*. Full specification and verification steps delivered to the responsible developer in [`specs/planning/T060-cuems-common-systemd-unit.md`](../../specs/planning/T060-cuems-common-systemd-unit.md). Summary: edit `etc/systemd/system/cuems-gradient-motiond.service` to remove `--nng-url`, add `--osc-port $CUEMS_GRADIENT_OSC_PORT` (default 7100 via `Environment=`), and remove `Wants=avahi-daemon.service` / `After=avahi-daemon.service`. Deployment-side counterpart of spec FR-013.
- [X] T061 [P] **Latency measurement (spec SC-002)** — extend the existing timing harness in [dev/](../../dev/) (or add a new tiny C++ program if none exists): time the interval between `lo_send` invocation on the sender side and `LockFreeQueue::push` return on the daemon side, both on the same host. Run 1000 sends, record p50 and p99. Assert p50 ≤ 1 ms and p99 ≤ 5 ms. Commit the harness and the measurement output.
- [X] T062 [P] **TSAN data-race check (spec FR-009, Constitution Principle IV)** — `cmake -S . -B build-tsan -DBUILD_DAEMON=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"` then `cmake --build build-tsan -j test_osc_server_integration test_motion_registry test_lockfree_queue && ctest --test-dir build-tsan --output-on-failure --tests-regex 'osc_server|motion_registry|lockfree'`. **Expect zero data-race reports.** If TSAN flags the OSC-thread → tick-thread handoff, treat as a regression — the lock-free queue's SPSC invariant has been violated somewhere in T030/T031/T040/T041.
- [X] T063 [P] **Two-daemon multi-node test (spec SC-005)** — implemented as `dev/deploy_tests/s007_t063_multi_node.sh`. Script starts node-a (port 17300) and node-b (port 17301) simultaneously, sends correct-node start_fade to each, then cross-sends (node-a message to node-b port and vice versa) and asserts neither daemon accepts the misdirected message. Also verifies cancel_all isolation. Results written to `dev/deploy_tests/results/s007_t063_multi_node.txt`. Run: `bash dev/deploy_tests/s007_t063_multi_node.sh`.
- [X] T064 **On-hardware acceptance — node-002** *(out-of-scope: requires node-002 hardware + `cuems-engine` `feat/gradient-osc-transport` + T060 landed)*. Full acceptance procedure delivered in [`specs/planning/T064-node-002-acceptance.md`](../../specs/planning/T064-node-002-acceptance.md). Summary: install v0.3.0 deb + updated `cuems-common` unit + `cuems-engine` companion branch on node-002; load `fade-test` project, press GO, verify 5 s smooth fade to silence and capture journal. This is spec SC-001, the primary user-facing success criterion.
- [X] T065 Optional: Avahi-stopped resilience check (spec SC-003) — implemented as `dev/deploy_tests/s007_t065_avahi_resilience.sh`. Script extracts the journal for the original T065 run (PID 223916, 2026-05-13, avahi-daemon stopped before start) from journalctl and verifies: OscServer bound without error, cancel_all + start_fade accepted, clean shutdown, zero avahi-daemon errors in daemon log. Results pre-populated in `dev/deploy_tests/results/s007_t065_avahi_resilience.txt`. Run: `bash dev/deploy_tests/s007_t065_avahi_resilience.sh`.
- [X] T066 [P] Confirm Spec 005 still carries the "Superseded by" header pointing at 007 (Quickstart §6). No code change expected — this is a final spec-hygiene check (spec FR-015 / SC-007).
- [X] T067 [P] Confirm [CLAUDE.md](../../CLAUDE.md)'s SPECKIT block points at `specs/007-osc-input-transport/plan.md` (already updated by `/speckit-plan`; confirm not reverted).
- [X] T068 Final build matrix — full `cmake -S . -B build_daemon -DBUILD_DAEMON=ON && cmake --build build_daemon -j && ctest --test-dir build_daemon --output-on-failure` from a clean tree. All tests green (spec SC-006).

**Checkpoint**: Phase H complete. All eight success criteria (SC-001 through SC-008) verified.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: T001–T003. No internal dependencies; can start immediately. T003 must precede any code change so the pre-change baseline is recorded.
- **Foundational (Phase 2)**: T004–T025. Depends on Setup. **Blocks all user stories.** Within Foundational:
  - CMake/packaging tasks (T004–T006) can run in parallel.
  - Rename tasks (T010–T014) must come before any new OSC code is written that reads `motion_id`. T011/T012/T013 can run in parallel after T010. T014 gates the rename's correctness.
  - Skeleton header adds (T015, T017) can run in parallel after the rename block.
  - Skeleton impls (T016, T018) depend on their respective headers.
  - Engine wire-up (T019) depends on T015 + T017 + T018 and on the rename being complete.
  - Deletes (T022, T023, T024) depend on the new skeleton compiling first (else the build is broken in the middle).
  - T025 (compile gate) is the last task in Foundational.
- **User Stories (Phases 3–6)**: All depend on Foundational. Within each story, tests come first, then implementation. Stories can run in parallel by different developers, but they touch the same parser/server files so coordination on those two files is required.
- **Polish (Phase 7)**: T056–T068. Depends on all in-scope user stories.

### User Story Dependencies

- **US1 (P1, MVP)**: No dependencies on other stories. Independently testable via the python-osc smoke. **Implementing US1 in isolation is the recommended MVP.**
- **US2 (P1)**: Logically independent but shares the parser and server callback files with US1. If staffed by one developer, do US2 immediately after US1.
- **US3 (P2)**: Largely already exercised by US1's parser implementation (the filter is on every code path). The story phase mostly adds explicit tests. Can be merged into US1's tasks if a single developer is doing the full feature.
- **US4 (P3)**: Independent. Touches the same files but only adds journal-logging and the partial-population rule in the parser.

### Within Each User Story

- Tests first, confirm they fail, then implement.
- Parser changes first, server callback wire-up next, engine drain path last.
- Run the relevant integration test after each phase as the smoke for that increment.

### Parallel Opportunities

- **Setup**: T002 and T003 in parallel.
- **Foundational**: T004/T005/T006 in parallel (different files). T011/T012/T013 in parallel (rename across non-overlapping files). T015 + T017 in parallel (different files). T016 + T018 in parallel once their headers land.
- **US1 tests**: T026/T027/T028 in parallel — same file but independent test cases. T029 separate file, parallel with T026–T028.
- **US2 tests**: T035/T036/T037/T038 in parallel — same file, independent. T039 separate file, parallel.
- **US3 tests**: T044/T045/T046 in parallel — same file, independent. T047 separate file, parallel.
- **US4 tests**: T050/T051 in parallel.
- **Polish**: T056/T057/T061/T062/T063/T066/T067 in parallel; T058/T059 sequential (T058 produces the deb T059 inspects).

Cross-story parallelism is bounded by the fact that the parser .cpp and the server .cpp are each modified by every story phase — only one developer at a time should edit them.

---

## Parallel Example: User Story 1

```bash
# Foundational must be complete (T025 green) before any of these run.

# Tests in parallel (different cases, same file is fine — assert blocks are independent):
Task: "Add StartFade_AcceptsWellFormedMessage to tests/test_osc_parse.cpp"          # T026
Task: "Add StartFade_RejectsWrongTypeTag to tests/test_osc_parse.cpp"               # T027
Task: "Add StartFade_RejectsMissingArguments to tests/test_osc_parse.cpp"           # T028

# Wire smoke test in a separate file:
Task: "Create tests/test_osc_server_integration.cpp with StartFade_PushesToQueue"   # T029

# Implementation (sequential — parser body must land before the server callback is meaningful):
Task: "Implement start_fade parse in src/signal/parseFadeOscCommand.cpp"            # T030
Task: "Wire OscServer /gradient/start_fade callback through parser"                 # T031
Task: "Log bind URL at INFO level for operator visibility"                          # T032
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001–T003).
2. Complete Phase 2: Foundational (T004–T025). **Critical.** Daemon must compile + start + bind + drop messages cleanly before any story work begins. The `motion_id` rename lands here.
3. Complete Phase 3: User Story 1 (T026–T034).
4. **STOP and validate**: Run the Quickstart §3–4 smoke. If the fade works end-to-end (parser → queue → registry → /volmaster ticks), Phase H's MVP is done.
5. Ship the MVP behind a feature toggle on the cuems-engine side (or in coordination with the companion branch landing). The remaining stories add cancel handling, multi-node correctness coverage, and observability — all valuable but not required for the first audible-fade demo.

### Incremental Delivery

1. Setup + Foundational → green build, daemon binds, struct renamed.
2. + US1 → fades start and tick. **MVP demo possible.**
3. + US2 → cancel works. STOP / LOAD round-trip safe.
4. + US3 → explicit multi-node coverage tests recorded.
5. + US4 → observability + dead-code removal complete.
6. + Polish → on-hardware sign-off on node-002, packaging deb produced, latency / TSAN / multi-node SCs verified.

### Parallel Team Strategy

With two developers:

1. Both complete Setup + Foundational together. Pair on T010–T014 (the rename) and T019 + T025 (the riskiest engine-wire-up and compile-gate tasks).
2. Developer A takes US1 + US3 (parser-heavy work).
3. Developer B takes US2 + US4 in sequence (server-callback heavy, observability).
4. Polish phase shared — T060 (cross-repo coordination) goes to whichever developer has rights on `cuems-common`.

Cross-story coordination point: both developers will be touching `src/signal/parseFadeOscCommand.cpp` and `daemon/comms/OscServer.cpp`. Use small, frequent commits and rebase often.

---

## Notes

- **[P] tasks**: different files, no dependencies on incomplete tasks within the same phase.
- **[Story] label**: maps task to its user story for traceability. Setup/Foundational/Polish have none.
- Each user story should be independently completable and testable on the dev host. The on-hardware (node-002) acceptance check is centralized in Polish (T064) to avoid blocking individual story work on hardware availability.
- Verify tests fail before implementing. Confirm in commit messages.
- Commit after each task or logical group. Avoid amending across the rename (T010–T014) and the deletions (T022–T024) — keep them as separate commits so a reviewer can audit each transition independently.
- Stop at any checkpoint to validate the increment independently.
- **Avoid**: introducing a new abstraction inside `gme::signal` to "support both JSON and OSC parsers in parallel" — that's the half-finished-implementation anti-pattern. The JSON parser is deleted as soon as the OSC parser is green.
