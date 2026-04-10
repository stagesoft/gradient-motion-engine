<!--
Sync Impact Report
==================
Version change: N/A (template) -> 1.0.0 (initial ratification)

Modified principles: N/A (all new)
  - I. Deterministic Evaluation (NEW)
  - II. Modular Architecture (NEW)
  - III. Library-First (NEW)
  - IV. Real-Time Safety (NEW)
  - V. Protocol-Agnostic Core (NEW)
  - VI. Documentation Standards (NEW)

Added sections:
  - Core Principles (6 principles)
  - Performance & Safety Standards
  - Development Workflow
  - Governance

Removed sections: None

Templates requiring updates:
  - .specify/templates/plan-template.md — Constitution Check section
    dynamically references constitution gates. No structural change
    needed. (check: pass)
  - .specify/templates/spec-template.md — Generic template, no
    constitution-specific references. (check: pass)
  - .specify/templates/tasks-template.md — Generic template, task
    phases are project-driven. (check: pass)
  - .specify/templates/commands/*.md — Directory does not exist.
    (check: N/A)

Follow-up TODOs: None
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

*Rationale*: Clean separation enables independent evolution of
each subsystem and reduces the blast radius of changes.

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

**Version**: 1.0.0 | **Ratified**: 2026-04-10 | **Last Amended**: 2026-04-10
