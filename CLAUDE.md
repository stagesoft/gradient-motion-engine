# gradient-motion-engine

Part of the **CUEMS** ecosystem — see the [`cuems-RELATIONS`](https://github.com/stagesoft/cuems-RELATIONS) repo for the system index, architecture diagram, and protocol/port map.

## Role

Timecode-driven motion and gradient evaluation engine with OSC output. C++17 (GCC, `-Wall -O3 -pthread`). MTC-synced via the `mtcreceiver` submodule; talks to the CUEMS engine over the NNG bus (`libnng`) and sends OSC via `liblo`. On CUEMS hosts it listens on the `gradient_osc_port` (7100 in `settings.xml`). Also uses nlohmann-json.

## Active technologies (per feature history)

- C++17 (GCC, `-Wall -O3 -pthread`), C++ standard library only for the core (`<cmath>`, `<vector>`, `<memory>`, `<string>`, `<functional>`) — `001-phase0-scaffold`, `002-gradient-curves`.
- `mtcreceiver` v2.0.0 (submodule) — `004-adapt-mtc-tick-v2`.
- NNG 1.10.1 (`libnng-dev`, C API `nng_bus0_open`) — `005-nng-bus-client`.
- liblo (OSC), nlohmann-json, RtMidi via mtcreceiver — `006-fade-registry-tick-loop`. All state in-memory (`FadeRegistry` map + fixed SPSC status queue).

## Build & release

Standard C++ submodule build (`git submodule update --init`, cmake/make). Release lineage: `rc_1` carries the fleet-wide MTC >24h work + `24h_extended_support` tag (tip `069f951`, 24h `mtcreceiver` `8a30d05`); `main` is the development line.

For additional per-feature context (project structure, shell commands), read the current plan at [specs/007-osc-input-transport/plan.md](specs/007-osc-input-transport/plan.md). Non-code artifacts follow the same `specs/planning/` convention as cuems-utils.

## Field notes

- Uses the shared `mtcreceiver` submodule — the 2s-resync-skip fix (`aa44894`) and the raw-wire-MTC timebase apply here too; see the mtcreceiver CLAUDE.md.
