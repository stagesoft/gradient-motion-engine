# Data Model: Phase 0 — Project Scaffold

**Date**: 2026-04-10
**Feature**: `001-phase0-scaffold`

## Entities

Phase 0 defines skeleton classes with minimal state. Fields marked *(Phase N)* are placeholders for future phases — they appear here for architectural context but are NOT implemented in Phase 0.

### Naming Convention

- **`GradientEngine*`** — top-level orchestration classes (application, engine). Named for the general-purpose signal evaluation system.
- **`Fade*`** — sub-elements specific to the fade-in/fade-out signal output type (FadeCommand, FadeRegistry, ActiveFade). These are one use case of the engine, designed to coexist with future signal output types (e.g., LFOs, envelopes, DMX sequences).

### GradientEngineApplication

The top-level daemon orchestrator. Lives in `daemon/` (NOT part of `libgradient_motion`). Owns the lifecycle and all subsystem references. Named `GradientEngine*` because it orchestrates the general engine, not just fades.

| Field | Type | Description |
|-------|------|-------------|
| `running_` | `bool` | Main loop continue flag; set `false` by signal handler |
| `initialized_` | `bool` | Whether `initialize()` completed successfully |
| `config_` | `unique_ptr<ConfigurationManager>` | Configuration storage |
| `logger_` | `CuemsLogger*` | Singleton logger pointer (when `ENABLE_CUEMS_LOGGER=ON`; nullptr when OFF) |

**Lifecycle states**: Constructed → Initialized → Running → Shutting Down → Destroyed

**State transitions**:
- Constructed → Initialized: `initialize(argc, argv)` succeeds
- Constructed → Destroyed: `initialize()` fails (help requested, or error)
- Initialized → Running: `run()` enters main loop
- Running → Shutting Down: signal received (SIGTERM/SIGINT)
- Shutting Down → Destroyed: `shutdown()` completes, destructor runs

### ConfigurationManager

Lives in `daemon/config/`. Stores configuration values from CLI arguments. XML parsing deferred to future phase.

| Field | Type | Default | Source |
|-------|------|---------|--------|
| `midiPort_` | `string` | `"Midi Through Port-0"` | `--midi-port` CLI arg |
| `logLevel_` | `string` | `"info"` | `--log-level` CLI arg |
| `confPath_` | `string` | `"/etc/cuems"` | `--conf-path` CLI arg |
| `controllerUrl_` | `string` | `""` | *(Phase N — from XML)* |
| `nngHubPort_` | `int` | `0` | *(Phase N — from XML)* |
| `mtcPort_` | `string` | `""` | *(Phase N — from XML)* |

**Validation rules**:
- `logLevel_` MUST be one of: `emergency`, `alert`, `critical`, `error`, `warning`, `notice`, `info`, `debug`
- `confPath_` MUST be a valid directory path (checked only when XML parsing is implemented)
- `midiPort_` is a free-form string matching ALSA MIDI port names

### libgradient_motion modules (empty in Phase 0)

The library source tree under `src/` contains one directory per `gme::*` namespace module. In Phase 0, each module directory exists but contains no implementation. Future phases populate them:

| Module | Namespace | Phase | Purpose |
|--------|-----------|-------|---------|
| `src/time/` | `gme::time` | Phase 2 | Timecode, clocks, scheduling |
| `src/motion/` | `gme::motion` | Future | Trajectories and spatial evaluation |
| `src/gradient/` | `gme::gradient` | Phase 1 | Keyframes and interpolation (curves) |
| `src/signal/` | `gme::signal` | Phase 3 | FadeCommand, LockFreeQueue — evaluated value representation |
| `src/osc/` | `gme::osc` | Phase 4 | OSC encoding and transport |
| `src/engine/` | `gme::engine` | Phase 4 | GradientEngine orchestrator, FadeRegistry, ActiveFade |

### Fade sub-elements (future phases — documented for context)

These entities live within `libgradient_motion` and represent the fade-specific signal output type:

| Entity | Namespace | Description |
|--------|-----------|-------------|
| `FadeCommand` | `gme::signal` | Command data struct for start/cancel/crossfade operations |
| `FadeRegistry` | `gme::engine` | Active fade map + per-tick evaluation |
| `ActiveFade` | `gme::engine` | One running fade instance with curve, OSC target, timing |
| `GradientEngine` | `gme::engine` | Top-level orchestrator wiring MTC ticks, command queue, and FadeRegistry |

The `GradientEngine` class is the wiring layer — it owns the tick loop, drains the command queue, and delegates to `FadeRegistry` for fade evaluation. Future signal output types would add their own registries alongside `FadeRegistry`, all driven by the same `GradientEngine` tick loop.

### CuemsLogger (external submodule — optional, controlled by `ENABLE_CUEMS_LOGGER`)

| Field | Type | Description |
|-------|------|-------------|
| `programSlug` | `static string` | Syslog identifier prefix (`"Cuems:<slug>"`) |
| `myObjectPointer` | `static CuemsLogger*` | Singleton instance |

**Usage in GradientEngine**: When enabled, instantiate with `CuemsLogger logger("GradientEngine")` in `GradientEngineApplication::initialize()`. All daemon code accesses logging through `daemon/logging.h` which dispatches to CuemsLogger or stdlib fallback.

### MtcReceiver (external submodule — not modified)

Compiled and linked in Phase 0 but NOT instantiated. Included here for dependency documentation.

| Field | Type | Description |
|-------|------|-------------|
| `mtcHead` | `static atomic<long int>` | Current timecode position in milliseconds |
| `isTimecodeRunning` | `static atomic<bool>` | Whether timecode is actively being received |
| `curFrameRate` | `static atomic<unsigned char>` | Current MTC frame rate code |

**Phase 0 scope**: Compiles and links only. Active use begins in Phase 2 (MTC Tick Source), integrated into `gme::time`.

## Relationships

```text
gradient-motiond (daemon/)
├── owns → GradientEngineApplication
│   ├── owns → ConfigurationManager (1:1, unique_ptr)
│   └── uses → CuemsLogger or stdlib fallback (via daemon/logging.h)
├── links → libgradient_motion (src/) — empty in Phase 0
└── links → mtcreceiver (compilation dependency only in Phase 0)

libgradient_motion (src/) — future architecture:
├── gme::time       → MtcTickSource (Phase 2)
├── gme::gradient   → Curve hierarchy, CurveFactory (Phase 1)
├── gme::signal     → FadeCommand, LockFreeQueue (Phase 3)
├── gme::engine     → GradientEngine, FadeRegistry, ActiveFade (Phase 4)
├── gme::osc        → OscSender (Phase 4)
└── gme::motion     → (reserved for future use)
```

## No Persistent Storage

Phase 0 has no persistent state, no files, no database. All configuration is transient (CLI args stored in memory for the process lifetime).
