# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[0.1.0]: https://github.com/stagesoft/gradient-motion-engine/releases/tag/v0.1.0
