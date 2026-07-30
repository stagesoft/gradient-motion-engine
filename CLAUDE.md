# gradient-motion-engine

Part of the **CUEMS** ecosystem — see the [`cuems-RELATIONS`](https://github.com/stagesoft/cuems-RELATIONS) repo for the system index, architecture diagram, and protocol/port map.

## Role

Timecode-driven motion and gradient evaluation engine with OSC output — runs as the daemon **`gradient-motiond`** (unit wired by cuems-common). C++17 (GCC, `-Wall -O3 -pthread`). MTC-synced via the `mtcreceiver` submodule; receives commands from the CUEMS engine over localhost UDP OSC and sends OSC out, both via `liblo`. Also uses nlohmann-json.

**Engine-side client:** `cuems-engine`'s `GradientClient` (`players/GradientClient.py`) is a fire-and-forget UDP OSC client targeting `gradient_osc_port` (7100 in `settings.xml`). Commands: `/gradient/start_fade`, `/gradient/cancel_motion <id>`, `/gradient/cancel_all`. The engine delegates cue fades here (loop-cue fades, ActionCue fades via `ActionHandler`) — the engine's loop only supervises; the fade curve itself is evaluated by this daemon.

## Active technologies (per feature history)

- C++17 (GCC, `-Wall -O3 -pthread`), C++ standard library only for the core (`<cmath>`, `<vector>`, `<memory>`, `<string>`, `<functional>`) — `001-phase0-scaffold`, `002-gradient-curves`.
- `mtcreceiver` v2.0.0 (submodule) — `004-adapt-mtc-tick-v2`.
- liblo (OSC), nlohmann-json, RtMidi via mtcreceiver — `006-fade-registry-tick-loop`. All state in-memory (`MotionRegistry` map + fixed SPSC command queue).
- liblo UDP OSC listener (`OscServer`, `127.0.0.1:<gradient_osc_port>`) as the inbound transport — `007-osc-input-transport`. Superseded the NNG bus client of `005-nng-bus-client`; `libnng` is **no longer a build or runtime dependency** (removed in v0.3.0, commit `538d992`), and the outbound NNG status channel is gone — motion status events are logged only.

## Build & release

Standard C++ submodule build (`git submodule update --init`, cmake/make). Release lineage: `rc_1` carries the fleet-wide MTC >24h work + `24h_extended_support` tag (tip `069f951`, 24h `mtcreceiver` `8a30d05`); `main` is the development line.

For additional per-feature context (project structure, shell commands), read the current plan at [specs/007-osc-input-transport/plan.md](specs/007-osc-input-transport/plan.md). Non-code artifacts follow the same `specs/planning/` convention as cuems-utils.

## Field notes

- Uses the shared `mtcreceiver` submodule — the 2s-resync-skip fix (`aa44894`) and the raw-wire-MTC timebase apply here too; see the mtcreceiver CLAUDE.md.
