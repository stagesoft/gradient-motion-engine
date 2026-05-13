# gradient-motion-engine Development Guidelines

Auto-generated from all feature plans. Last updated: 2026-04-23

## Active Technologies
- C++17 (GCC, `-Wall -O3 -pthread`) + None (C++ standard library only — `<cmath>`, `<vector>`, `<memory>`, `<string>`, `<functional>`) (002-gradient-curves)
- C++17 (GCC, `-Wall -O3 -pthread`) + mtcreceiver v2.0.0 (submodule, pinned at `59fc76e`), (004-adapt-mtc-tick-v2)
- N/A (in-memory adapter; no persistence) (004-adapt-mtc-tick-v2)
- C++17 (GCC, `-Wall -O3 -pthread`) + NNG 1.10.1 (`libnng-dev`, C API — `nng_bus0_open`, (005-nng-bus-client)
- N/A — all state is in-memory. The queue is a fixed-size array; (005-nng-bus-client)
- C++17 (GCC, `-Wall -O3 -pthread`) + liblo (OSC sending), NNG 1.10.1 (already linked), nlohmann-json (already linked), RtMidi via mtcreceiver submodule (already linked) (006-fade-registry-tick-loop)
- N/A — all state in-memory (`std::unordered_map` inside `FadeRegistry`, fixed SPSC queue for status) (006-fade-registry-tick-loop)

- C++17 (GCC, `-Wall -O3 -pthread`) (001-phase0-scaffold)

## Project Structure

```text
src/
tests/
```

## Commands

# Add commands for C++17 (GCC, `-Wall -O3 -pthread`)

## Code Style

C++17 (GCC, `-Wall -O3 -pthread`): Follow standard conventions

## Recent Changes
- 006-fade-registry-tick-loop: Added C++17 (GCC, `-Wall -O3 -pthread`) + liblo (OSC sending), NNG 1.10.1 (already linked), nlohmann-json (already linked), RtMidi via mtcreceiver submodule (already linked)
- 005-nng-bus-client: Added C++17 (GCC, `-Wall -O3 -pthread`) + NNG 1.10.1 (`libnng-dev`, C API — `nng_bus0_open`,
- 004-adapt-mtc-tick-v2: Added C++17 (GCC, `-Wall -O3 -pthread`) + mtcreceiver v2.0.0 (submodule, pinned at `59fc76e`),


<!-- MANUAL ADDITIONS START -->
<!-- MANUAL ADDITIONS END -->

<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan
<!-- SPECKIT END -->
