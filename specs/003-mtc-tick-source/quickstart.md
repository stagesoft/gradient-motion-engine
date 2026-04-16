# Quickstart: Phase 2 — MTC Tick Source

**Branch**: `003-mtc-tick-source` | **Date**: 2026-04-16

---

## Smoke Test: Verify Quarter-Frame Ticks Fire

This scenario validates the complete Phase 2 deliverable: `MtcTickSource` wired to a real or simulated MTC source, confirming the callback fires at the expected rate.

### Prerequisites

- `librtmidi-dev` >= 5.0 installed
- `libasound2-dev` installed (ALSA backend)
- Build target `gradient_motion` compiles without error
- A virtual MIDI port or loopback (e.g., `rtpmidid`, `aconnect`, or `JACK` with `a2jmidid`) providing MTC output

### Build

```bash
cd /disk/Projects/StageLab/gradient-motion-engine
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target gradient_motion
```

### Minimal Integration Test (C++)

```cpp
// tests/test_mtc_tick_source.cpp  (manual smoke test, not automated)
#include "src/time/MtcTickSource.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

int main() {
    std::atomic<int> tickCount{0};
    std::atomic<long> lastMs{0};

    gme::time::MtcTickSource src;

    src.setTickCallback([&](long ms) {
        tickCount.fetch_add(1);
        lastMs.store(ms);
    });

    // Replace "MTC" with the actual substring of your MIDI port name
    auto err = src.start("MTC");
    if (err != gme::time::MtcStartError::kOk) {
        std::cerr << "Failed to open MIDI port\n";
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::cout << "Ticks in 5s: " << tickCount.load() << "\n";
    std::cout << "Last MTC ms: " << lastMs.load() << "\n";
    std::cout << "Is running:  " << src.isRunning() << "\n";

    // At 25 fps: expect ~1000 ticks (200 Hz × 5 s)
    // At 30 fps: expect ~1200 ticks (240 Hz × 5 s)
    return 0;
}
```

### Expected Results

| Condition | Expected |
|-----------|----------|
| MTC running at 25 fps for 5 s | ~1000 ticks (±10%) |
| `lastMs` after 5 s of MTC from 00:00:00:00 | ~5000 ms |
| `isRunning()` while MTC active | `true` |
| `isRunning()` after MTC stops (>100 ms) | `false` |
| `getMtcMs()` before `start()` | `0` |
| `start("nonexistent-port")` | returns `MtcStartError::kPortNotFound` |

---

## Unit Test Scenarios (no MIDI hardware required)

These scenarios are exercised in `tests/test_mtc_tick_source.cpp` (automated):

### Scenario A: Callback invocation count

Directly call `MtcReceiver::onQuarterFrame(1234)` from a test and verify the registered callback receives `1234`.

### Scenario B: Both decode sites fire

Simulate `decodeQuarterFrame()` by calling the internal `onQuarterFrame` at both update sites (mocked). Verify callback fires twice per full-frame decode cycle (once at site 1, once at site 2).

### Scenario C: Null callback safety

Leave `onQuarterFrame` unset and invoke the callback guard path — verify no crash (null-check protection).

### Scenario D: `getMtcMs()` before `start()`

Construct `MtcTickSource` without calling `start()`. Call `getMtcMs()` — expect `0`.

### Scenario E: Invalid port returns error

Call `start("__no_such_port__")` when no matching MIDI port exists — expect `MtcStartError::kPortNotFound`.

---

## Submodule Smoke Check

After the upstream `mtcreceiver` changes are merged and the submodule pin is advanced:

```bash
cd /disk/Projects/StageLab/gradient-motion-engine
git submodule status
# Expected: new commit hash for mtcreceiver, not 63ce3de
git -C mtcreceiver log --oneline -3
# Expected: top commit includes onQuarterFrame addition
```
