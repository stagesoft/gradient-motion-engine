# Data Model: Phase 2 — MTC Tick Source

**Branch**: `003-mtc-tick-source` | **Date**: 2026-04-16

---

## Entity 1: `MtcTickSource` (`gme::time`)

**File**: `src/time/MtcTickSource.h` / `src/time/MtcTickSource.cpp`

**Purpose**: Adapter that wraps `MtcReceiver`'s static-member interface behind an object-oriented facade. Owns the MIDI port lifecycle and routes the quarter-frame callback to registered consumers.

### State

| Field | Type | Ownership | Description |
|-------|------|-----------|-------------|
| `receiver_` | `std::unique_ptr<MtcReceiver>` | owned | Created in `start()`, destroyed on scope exit. Null until `start()` is called. |

### Public Interface

| Method | Signature | Precondition | Returns |
|--------|-----------|--------------|---------|
| `setTickCallback` | `void setTickCallback(std::function<void(long)> cb)` | Must be called before `start()` | — |
| `start` | `MtcStartError start(const std::string& midiPort)` | `setTickCallback()` already called | `MtcStartError::kOk` on success; `kNoPortsAvailable` or `kPortNotFound` on failure |
| `getMtcMs` | `long getMtcMs() const` | None (safe before `start()`) | Current MTC head in ms (0 if never started) |
| `isRunning` | `bool isRunning() const` | None (safe before `start()`) | `true` if MTC running, `false` otherwise |

### Constraints

- **One instance per process**: `MtcReceiver` uses process-global static members. Constructing two `MtcTickSource` instances in the same process produces undefined behavior. Document with `static_assert` or a comment in the header.
- **Initialization order**: `setTickCallback()` → `start()`. Calling `start()` without a callback means ticks fire but are silently discarded (null-check in `MtcReceiver`).
- **Thread safety**: `getMtcMs()` and `isRunning()` are safe to call from any thread (atomic reads). `start()` and `setTickCallback()` are initialization-time methods; concurrent access is undefined.

---

## Entity 2: `MtcReceiver` extension (upstream `stagesoft/mtcreceiver`)

**Files**: `mtcreceiver/mtcreceiver.h`, `mtcreceiver/mtcreceiver.cpp`

**Purpose**: Existing MTC decoding class extended with a static quarter-frame callback and an optional port-index constructor parameter.

### New Members

| Member | Type | Location | Description |
|--------|------|----------|-------------|
| `onQuarterFrame` | `static std::function<void(long)>` | `mtcreceiver.h` (public) | Fires after every `mtcHead` update in `decodeQuarterFrame()`. Null-checked before invocation. |

### Constructor Extension

| Parameter | Type | Default | Position | Description |
|-----------|------|---------|----------|-------------|
| `portIndex` | `unsigned int` | `0` | Last (after `queueSizeLimit`) | RtMidi port index to open. Appended after existing params to preserve positional backward compatibility. |

### Callback Invocation Sites

Both sites are inside `MtcReceiver::decodeQuarterFrame()`:

1. **Site 1** — per-quarter running update (fires on every quarter frame):
   ```
   mtcHead.store(mtcHead.load() + 250 / curFrame.getFps());
   if (onQuarterFrame) onQuarterFrame(mtcHead.load());
   ```

2. **Site 2** — per-complete-frame decode (fires after all 8 QF received):
   ```
   mtcHead.store(curFrame.toMilliseconds());
   if (onQuarterFrame) onQuarterFrame(mtcHead.load());
   ```

**Note**: `decodeFullFrame()` (SysEx seek/locate) does NOT invoke the callback — it is a position reset, not a running tick.

---

## Entity 3: `Quarter-frame tick` (value, not a class)

**Type**: `long` (milliseconds)  
**Source**: `MtcReceiver::mtcHead.load()` at moment of callback invocation  
**Rate**: 8× per MTC frame — 200 Hz at 25 fps, 192 Hz at 24 fps, ~240 Hz at 30 fps  
**Precision**: ±1 quarter-frame duration (≤ 5 ms at 25 fps)  
**Consumers**: `GradientEngine` tick thread (Phase 4)

---

## State Transitions

```
MtcTickSource lifecycle:
  [constructed] → setTickCallback() → [callback registered]
                                            ↓
                                        start(portName)
                                            ↓
                              [MtcReceiver constructed, port open]
                                            ↓
                              [ticks firing while MTC running]
                                            ↓
                              [destructor: MtcReceiver destroyed, port closed]
```

```
MtcReceiver::onQuarterFrame lifecycle:
  [nullptr / unset]
        ↓  MtcTickSource::setTickCallback()
  [set to consumer lambda]
        ↓  MtcTickSource::start()
  [MtcReceiver constructed — callback fires from MIDI thread]
        ↓  MtcTickSource destructor
  [MtcReceiver destroyed — no further callbacks]
```
