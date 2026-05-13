<!--
Sync Impact Report
==================
Version change: 1.2.0 -> 1.3.0 (MINOR: added Deploy-Test Scripts workflow
  rule — codifies dev/deploy_tests/ layout, naming convention, and
  partial-pass pattern for tasks that require daemon-level or
  multi-process verification outside CTest).

Modified principles: None.

Added principles: None.

Added sections:
  - Development Workflow — new "Deploy-Test Scripts" bullet covering:
      * Folder: dev/deploy_tests/
      * Naming: sNNN_tXXX_<slug>.sh / sNNN_<tool>.cpp
      * Results: dev/deploy_tests/results/sNNN_tXXX_<slug>.txt
      * Scope: when to use deploy-test vs CTest
      * Partial-pass pattern for hardware-only steps

Templates requiring updates:
  - .specify/templates/plan-template.md — No structural change
    needed; rule is workflow-level.
    (check: pass)
  - .specify/templates/spec-template.md — No constitution-specific
    references. (check: pass)
  - .specify/templates/tasks-template.md — No constitution-specific
    references. (check: pass)

Follow-up TODOs: None.
==================
-->

# Gradient Motion Engine Constitution

## Core Principles

### I. Deterministic Evaluation

The evaluation pipeline MUST be a pure function of its inputs
(timecode and configuration). Identical inputs MUST produce
identical outputs across all runs and platforms.

- No hidden mutable state in the core library.
- No reliance on wall-clock time, randomness, or
  environment-dependent behavior during evaluation.
- Non-determinism (e.g., network jitter in OSC delivery) MUST
  be confined to the transport layer and MUST NOT feed back
  into evaluation.

*Rationale*: Determinism enables reproducible debugging, offline
validation, and confidence that test results reflect production
behavior.

### II. Modular Architecture

Each domain (`gme::time`, `gme::motion`, `gme::gradient`,
`gme::signal`, `gme::osc`, `gme::engine`) MUST be independently
compilable and testable.

- No circular dependencies between modules.
- Inter-module contracts MUST be defined through explicit
  interfaces, not shared mutable state.
- Adding or removing a module MUST NOT require changes to
  unrelated modules.
- **Module responsibilities**:
  - `gme::time` — MTC clock sources and tick scheduling.
  - `gme::gradient` — curve types, curve factory, curve
    evaluation.
  - `gme::motion` — motion lifecycle (`MotionRegistry`,
    `MotionFactory`) and the polymorphic motion hierarchy
    (`IMotion` and its concrete subclasses: scalar
    `FadeMotion`, N-dimensional `VectorMotion<N>`, future
    crossfade / path motions).
  - `gme::signal` — transport-agnostic command, frame, and
    status record types plus their lock-free queues.
  - `gme::osc` — OSC transport layer (address handles,
    send functions).
  - `gme::engine` — orchestrator (`GradientEngine`) that
    wires the other modules together into a running pipeline.

*Rationale*: Clean separation enables independent evolution of
each subsystem and reduces the blast radius of changes. Calling
out the per-module responsibility prevents motion-specific code
from silently migrating into `gme::engine` or `gme::signal`.

### III. Library-First

All core functionality MUST live in `libgradient_motion`. The
daemon (`gradient-motiond`) is a thin runtime consumer.

- Core evaluation logic MUST NOT depend on systemd, daemon
  lifecycle, or deployment infrastructure.
- New features MUST be implemented and tested at the library
  level before being wired into the daemon.
- `libgradient_motion` MUST be embeddable in third-party
  applications without pulling in daemon dependencies.

*Rationale*: Keeping the library self-contained maximizes reuse
and ensures the engine can be embedded in contexts beyond the
daemon (e.g., offline tools, test harnesses, other applications).

### IV. Real-Time Safety

Code in the evaluation hot path MUST avoid dynamic allocation,
exceptions, and blocking I/O.

- Memory for frame evaluation MUST be pre-allocated or use
  fixed-size buffers.
- Latency budgets MUST be defined for each pipeline stage and
  enforced through benchmarks.
- Logging in the hot path MUST be lock-free or deferred to a
  background thread.

*Rationale*: The engine runs as a continuous real-time service
under systemd. Unbounded latency spikes break the
timecode-to-OSC contract.

### V. Protocol-Agnostic Core

OSC is isolated to the output layer (`gme::osc`). The core
library MUST NOT assume any specific transport protocol.

- Evaluation produces `gme::signal::Frame` objects; serialization
  to a wire format is a separate, pluggable concern.
- Adding a new output format (e.g., MIDI, DMX, WebSocket) MUST
  NOT require changes to evaluation logic.
- Transport configuration MUST be external to the library
  (daemon config or caller-provided).

*Rationale*: Protocol isolation ensures the engine's value
proposition (deterministic motion/gradient evaluation) is not
coupled to a single transport technology.

### VI. Documentation Standards

All public-facing methods, classes, and entities MUST contain
complete docstrings suitable for automated documentation
generation.

Every public docstring MUST include:

- **Brief description**: A single-line summary of purpose.
- **Parameters**: Each parameter documented with name, type,
  and description. Default values MUST be stated.
- **Long description**: An expanded explanation when the brief
  description is insufficient to convey behavior, constraints,
  or usage context. MAY be omitted for trivially obvious APIs.
- **Exceptions/Errors**: Every error condition the caller MUST
  handle, including the error type and the condition that
  triggers it.
- **Example code**: At least one usage example demonstrating
  the typical call pattern. Examples MUST compile and produce
  the described output.

Additional rules:

- Internal (non-public) helpers are exempt but SHOULD include
  a brief description when non-obvious.
- Docstrings MUST be kept in sync with implementation. A PR
  that changes public API behavior without updating the
  corresponding docstring MUST NOT be merged.
- The project MUST support a documentation generation toolchain
  (e.g., Doxygen) that consumes these docstrings. Generated
  output MUST build without warnings on CI.

*Rationale*: `libgradient_motion` is designed for embedding in
third-party applications. Consumers who cannot read the source
depend entirely on generated documentation to understand
contracts, error modes, and correct usage.

### VII. Extensibility via Abstraction

Adding a new motion type, curve type, or output payload shape
MUST NOT require modifications to the motion registry, command
dispatcher, or tick loop.

- New motion types MUST extend `gme::motion::IMotion`. The
  registry, factory, and tick loop interact with motions
  exclusively through this interface.
- New curve types MUST register with
  `gme::gradient::CurveFactory`; no consumer code changes.
- New output transports MUST implement the transport-agnostic
  send interface (see Principle V); no evaluation-logic
  changes.
- Abstract interfaces expose the subset of state that ALL
  subclasses share (common lifecycle fields — `motion_id`,
  `osc_key`, `start_mtc_ms`, `duration_ms`, `completed`,
  `consecutive_osc_failures`). Per-type data (curves, payload
  arrays, transport handles) MUST live in the derived type,
  never in the base.
- Scalar and N-dimensional motion payloads share a single
  `IMotion` contract. Payload arity is a compile-time template
  parameter of the concrete motion type (e.g.
  `VectorMotion<N>` with `N ∈ {2, 3, 4}` using
  `std::array<float, N>`), not a runtime branch in the
  registry.
- The supersede rule is global: one motion per output path at
  any given time, keyed by the composite `"host:port:path"`.
  Multi-path outputs are represented by multiple simultaneous
  motions; multi-dimensional payloads are represented by a
  single motion whose transport message carries N arguments.

*Rationale*: The engine's identity is a deterministic,
embeddable core with stable lifecycle semantics. New motion
shapes (scalar fades today; N-dimensional vector motions,
crossfades, and other payloads in the future), new curves, and
new transports will accrete over time. Open-closed extensibility
bounds the risk of regressions in load-bearing infrastructure
and keeps per-type variation isolated from shared lifecycle
code.

## Performance & Safety Standards

- **Allocation budget**: Zero heap allocations per evaluation
  frame in steady state. Startup and configuration loading are
  exempt.
- **Latency target**: Each full pipeline tick (Timecode -> Signal)
  MUST complete within a configurable deadline. The default
  target is < 1 ms on reference hardware.
- **Thread safety**: `libgradient_motion` evaluation MUST be safe
  to call from a single dedicated thread without external
  locking. Multi-threaded use MUST be documented per-API.
- **Error handling**: The library MUST use explicit error types
  (e.g., `std::expected` or error codes). Exceptions MUST NOT
  cross library boundaries.
- **Build reproducibility**: Builds MUST be reproducible given
  the same toolchain and source revision. Compiler flags and
  dependency versions MUST be pinned.
- **Virtual dispatch**: Acceptable at per-motion granularity
  in the tick loop (one virtual call per active motion per
  tick, bounded by the motion count). NOT acceptable at
  per-sample or per-curve-evaluation granularity; curve
  evaluation is inlined via `gme::gradient::Curve::evaluate`.
  Virtual calls MUST NOT introduce heap allocation, blocking
  I/O, or exception escape.

## Development Workflow

- **Branching**: Feature work MUST happen on dedicated branches.
  Direct pushes to `main` are prohibited.
- **Testing**: All new functionality MUST include unit tests at
  the library level. Integration tests MUST cover daemon
  configuration loading and OSC output.
- **Code review**: All changes MUST be reviewed before merging.
  Reviews MUST verify compliance with this constitution.
- **Commit discipline**: Commits MUST be atomic and
  self-describing. Each commit MUST compile and pass tests
  independently.
- **Documentation**: Public API changes MUST include updated
  docstrings per Principle VI. Behavioral changes MUST be
  reflected in relevant docs.
- **Specification & Planning Layout**: Project documentation
  artifacts MUST be partitioned by audience and lifecycle:
  - `specs/NNN-feature/` — per-feature spec, plan, tasks, and
    supporting design artifacts. Created and owned by the
    feature branch that introduces them.
  - `specs/planning/` — cross-cutting planning documents that
    span multiple features (refactor roadmaps, phase handoff
    notes, migration strategies, architectural surveys). These
    documents are scoped to development workflow and are not
    consumer-facing.
  - Top-level `docs/` — reserved for end-user documentation
    and generated API reference output (Doxygen or equivalent,
    per Principle VI). Hand-written development planning
    artifacts MUST NOT live here, to prevent collisions with
    auto-generated content and to keep the user-facing
    surface coherent.

  *Rationale*: Mixing dev-internal planning with generated
  API docs forces tooling and consumers to filter intent-mixed
  content. Clear partitioning lets each audience (feature
  authors, cross-feature planners, library consumers) reach
  what they need without sifting through artifacts addressed
  to someone else.

- **Deploy-Test Scripts**: Tasks that require daemon-level orchestration,
  multi-process coordination, or deployment-path verification and cannot
  be expressed as CTest targets MUST be implemented as Bash scripts in
  `dev/deploy_tests/`.

  **Naming convention**: `sNNN_tXXX_<slug>.sh`, where `NNN` is the
  zero-padded spec number and `XXX` is the zero-padded task ID from
  `tasks.md` (e.g., `s007_t034_smoke.sh`). Support tools written in
  C++ (e.g., OSC CLI senders) follow the same spec prefix:
  `sNNN_<tool>.cpp`.

  **Results**: Each script MUST write its output to
  `dev/deploy_tests/results/sNNN_tXXX_<slug>.txt`. Scripts MUST exit
  0 on pass and 1 on failure, and MUST print a `RESULT: PASS` /
  `RESULT: FAIL` summary line so CI or a reviewer can scan at a
  glance. Pre-captured results (e.g., journal extracts from one-off
  hardware runs) MUST be committed alongside the script.

  **Scope**: A deploy-test script is appropriate when the verification
  requires: (a) starting the real daemon binary, (b) sending network
  traffic (OSC, UDP), (c) multi-daemon orchestration, (d) journal
  inspection, or (e) hardware/environment conditions that CTest cannot
  replicate (Avahi stopped, closed ports, MTC source absent). Pure
  unit and integration tests that can run in the build sandbox belong
  in `tests/` as CTest targets per Principle III.

  **Partial-pass pattern**: When a task has a dev-host-verifiable
  portion and a hardware-only portion (e.g., requires a live MTC
  source on node-002), the script MUST run the dev-host portion and
  exit 0 with a `SKIP:` annotation for the hardware-only steps,
  accompanied by explicit reproduction instructions in the script.

  *Rationale*: Not all acceptance criteria can be expressed as
  hermetic CTest targets. Codifying the folder, naming, and output
  conventions prevents each feature from inventing its own
  verification layout, keeps deploy artifacts discoverable across
  specs, and makes it unambiguous which tasks have been verified and
  which require hardware.

## Governance

This constitution is the authoritative source of project-wide
development principles. It supersedes ad-hoc practices,
informal agreements, and undocumented conventions.

**Amendment procedure**:

1. Propose changes via a dedicated PR with rationale.
2. Changes MUST be reviewed and approved by at least one
   maintainer.
3. The PR MUST include a migration plan for any code or
   process affected by the change.
4. Version MUST be incremented per semantic versioning:
   - MAJOR: Principle removal or backward-incompatible
     redefinition.
   - MINOR: New principle or materially expanded guidance.
   - PATCH: Clarifications, wording, or non-semantic
     refinements.

**Compliance review**: Every PR review MUST include a check
against these principles. Violations MUST be resolved before
merge or explicitly justified in the Complexity Tracking
section of the implementation plan.

**Version**: 1.3.0 | **Ratified**: 2026-04-10 | **Last Amended**: 2026-05-13
