# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2026-05-13 — Phase H: OSC Input Transport

Replaces the NNG bus-client inbound command transport with a localhost UDP OSC listener.
Full spec and design: [specs/007-osc-input-transport/](specs/007-osc-input-transport/).

### Added

* **`OscServer`** (`daemon/comms/OscServer.h/.cpp`): liblo-based UDP listener that binds on
  `0.0.0.0:<port>` and filters by `node_name`.  Registers three method handlers:
  `/gradient/start_fade` (type tag `sssisffhiss`), `/gradient/cancel_motion` (`ss`),
  `/gradient/cancel_all` (`s`).  PIMPL pattern keeps liblo headers out of library consumers.
* **`parseFadeOscCommand`** (`src/signal/parseFadeOscCommand.h/.cpp`): pure-C++ free function
  that validates type-tag, `node_name` filter, field constraints, and `curve_params_json`
  (nlohmann::json), returning `Ok / NodeMismatch / MissingField / TypeError / UnknownCommand`.
  Partial-population rule: `motion_id` and `type` are set before any early return so callers
  can log the offending command.
* **`--osc-port`** CLI flag (default `7100`) and `CUEMS_GRADIENT_OSC_PORT` environment
  variable override; reads `<gradient_osc_port>` from `settings.xml` as a third fallback.
* **`test_osc_parse`**: 14 unit test cases (parse-level, no network) covering all three
  addresses, node-name filter, field validation, and partial-population.
* **`test_osc_server_integration`**: 3 real-loopback integration tests (start_fade push,
  cancel_all push, node-mismatch drop) using liblo's client side.

### Changed

* **`FadeCommand`**: renamed `fade_id` → `motion_id` and `partner_fade_id` →
  `partner_motion_id` for ecosystem consistency (`motion_id` is the generic identifier used
  by all current and future motion types).
* **`GradientEngine`**: replaced `nngClient_` with `oscServer_`; removed `statusQueue_`;
  `onTick` drain loop unchanged, with added `CANCEL_MOTION`/`CANCEL_ALL` debug log lines.
* **`MotionRegistry`**: constructor no longer accepts a `statusQueue` parameter; all status
  events are delivered through the `statusDirect_` callback.

### Removed

* **NNG bus client** (`daemon/comms/NngBusClient.h/.cpp`) and all NNG CMake / packaging
  references (`find_package(nng)`, `libnng-dev` in `debian/control`).
* **`StatusEmitRequest`** (`src/signal/StatusEmitRequest.h`) and the status-emit queue.
* **JSON `parseFadeCommand`** and `classifyParseOutcome` helpers from `FadeCommand.cpp/.h`.
* **`test_nng_parse`** and **`test_nng_integration`** test targets.
* **`--nng-url`** CLI flag; replaced by `--osc-port`.

---

## [0.1.0] - 2026-04-23

First public release: timecode-driven motion and gradient evaluation with OSC output.

### Added

* **`libgradient_motion`**: C++ library with modular namespaces (`gme::time`, `gme::gradient`, `gme::motion`, `gme::signal`, `gme::osc`, `gme::engine`) for deterministic curve evaluation, motion registry, and OSC-friendly signal paths.
* **Gradient / curve system**: pluggable curve types (e.g. linear, sigmoid, bezier, resampled), crossfade helpers, and factory-based construction for fade-style motion.
* **MTC timecode**: MTC tick source and adapter for **mtcreceiver** v2, including `setTickCallback` / `start` / lifecycle and one-instance-per-process contract documentation.
* **Inter-process comms**: NNG bus client for receiving external commands and status-related integration (with lock-free queue and parse helpers where applicable).
* **`gradient-motiond`**: Linux daemon (CLI, configuration, engine wiring, NNG, optional CuemsLogger) that runs the evaluation pipeline and emits OSC.
* **Fade / motion path**: motion registry, fade motion implementation, and tick-aligned evaluation loop aligned with the fade-registry feature set.
* **Tests**: unit and integration tests for curves, MTC, NNG, lock-free queue, fade/motion registry, and related behaviour.

[0.3.0]: https://github.com/stagesoft/gradient-motion-engine/releases/tag/v0.3.0
[0.1.0]: https://github.com/stagesoft/gradient-motion-engine/releases/tag/v0.1.0
