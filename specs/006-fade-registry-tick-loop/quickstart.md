# Quickstart: Phase 4 — Fade Registry, Tick Loop & OSC Sender

**Branch**: `006-fade-registry-tick-loop`

## Prerequisites

Phases 1–3 must be complete:
- `libgradient_motion` builds cleanly (all existing tests pass).
- `mtcreceiver` and `cuemslogger` submodules are initialised.
- `libnng-dev`, `liblo-dev`, `nlohmann-json3-dev`, `librtmidi-dev`, `libasound2-dev`, `libtinyxml2-dev` are installed.

```bash
git submodule update --init --recursive
sudo apt install libnng-dev liblo-dev nlohmann-json3-dev librtmidi-dev libasound2-dev libtinyxml2-dev
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_DAEMON=ON -DENABLE_CUEMS_LOGGER=OFF
cmake --build build -j$(nproc)
```

## Run Unit Tests

```bash
cmake --build build --target test -- ARGS="--label-regex unit"
# or
cd build && ctest -L unit --output-on-failure
```

## Run the New Phase 4 Tests

```bash
cd build && ctest -R "test_fade_registry" --output-on-failure
```

## What Phase 4 Adds

| New file | Purpose |
|----------|---------|
| `src/engine/ActiveFade.h` | Data struct for one running fade |
| `src/engine/FadeRegistry.h/.cpp` | Registry: addFade, cancelFade, cancelAll, tick |
| `src/engine/GradientEngine.h/.cpp` | Wires MtcTickSource + NngBusClient + FadeRegistry |
| `src/osc/OscSender.h/.cpp` | Thin liblo UDP float-send wrapper |
| `daemon/comms/NngBusClient.h/.cpp` | Extended: status worker thread + SPSC status queue |
| `tests/test_fade_registry.cpp` | Unit tests: curve eval, completion, cancel, supersede |
| `tests/test_fade_registry_bench.cpp` | OSC loopback benchmark: SC-007 (50 fades, 2 ms budget) |

## Key Design Choices (see research.md for full rationale)

- **Tick thread = RtMidi MIDI thread**: `GradientEngine::onTick` is registered as the MtcTickSource callback. There is no separate "tick thread" — the MTC quarter-frame callback drives evaluation directly.
- **OscSender is stateless**: `lo_address` lifetime is owned by `ActiveFade`, not `OscSender`. Each fade pre-builds its address at registration time (`lo_address_new`) and frees it on removal (`lo_address_free`).
- **Status worker is in NngBusClient**: The tick thread pushes `StatusEmitRequest` tuples onto a 64-slot SPSC queue; the status worker thread in `NngBusClient` pops and calls `sendStatus`. This keeps NNG I/O off the tick thread (FR-006b).
- **Crossfade is Phase 7**: `ActiveFade::crossfade_partner` is present in the data model but always `nullptr` in Phase 4. `START_CROSSFADE` commands are logged and dropped.

## CMakeLists.txt Changes Required

In `src/CMakeLists.txt`, replace the stub `osc/osc.cpp` and `engine/engine.cpp` entries with real sources:

```cmake
osc/OscSender.cpp
engine/FadeRegistry.cpp
engine/GradientEngine.cpp
```

And add liblo to the link:

```cmake
if(LIBLO_FOUND)
    target_link_libraries(gradient_motion PUBLIC ${LIBLO_LIBRARIES})
    target_include_directories(gradient_motion PUBLIC ${LIBLO_INCLUDE_DIRS})
endif()
```

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(test_fade_registry test_fade_registry.cpp)
target_include_directories(test_fade_registry PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
target_link_libraries(test_fade_registry PRIVATE gradient_motion ${LIBLO_LIBRARIES})
add_test(NAME test_fade_registry COMMAND test_fade_registry)
set_tests_properties(test_fade_registry PROPERTIES LABELS "unit")

add_executable(test_fade_registry_bench test_fade_registry_bench.cpp)
target_include_directories(test_fade_registry_bench PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
target_link_libraries(test_fade_registry_bench PRIVATE gradient_motion ${LIBLO_LIBRARIES})
add_test(NAME test_fade_registry_bench COMMAND test_fade_registry_bench)
set_tests_properties(test_fade_registry_bench PROPERTIES LABELS "integration")
```
