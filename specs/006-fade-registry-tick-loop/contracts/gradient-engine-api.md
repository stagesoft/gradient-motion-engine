# Contract: `gme::engine::GradientEngine`

**File**: `src/engine/GradientEngine.h/.cpp`  
**Namespace**: `gme::engine`  
**Thread model**: `initialize()` and `shutdown()` must be called from the same thread. After `initialize()`, the engine owns three concurrent threads internally (RtMidi MIDI thread for ticks, NNG recv thread, NNG fallback drain thread). All public methods are safe to call from the owning thread.

---

## `initialize(config)`

```cpp
struct GradientEngineConfig {
    std::string midiPort;     // Substring to match MIDI port name.
    std::string nngUrl;       // e.g. "tcp://127.0.0.1:9093"
    std::string nodeName;     // Own node name for NNG filtering.
};

bool initialize(const GradientEngineConfig& config);
```

**Brief**: Open MIDI port, open NNG socket, start recv + drain + status worker threads. Register tick callback.

**Parameters**:
- `config.midiPort` — MIDI port name substring for `MtcTickSource::start()`.
- `config.nngUrl` — NNG dial URL for `NngBusClient::start()`.
- `config.nodeName` — Node name used by `NngBusClient` to filter incoming commands.

**Returns**: `true` on success. `false` if MIDI port not found or NNG socket failed to open. Error details are logged.

**Throws**: Never.

**Preconditions**: Must be called before `shutdown()`. Must not be called twice.

**Postconditions on success**:
- MTC tick callback is registered; `FadeRegistry::tick` fires on each MTC quarter frame.
- `NngBusClient` is dialing (non-blocking); recv + drain + status worker threads are running.

---

## `shutdown()`

**Brief**: Graceful teardown. Stops new command ingestion, drains in-flight status, closes NNG socket, stops MIDI.

**Behaviour** (sequence per spec FR-013 / Phase 5 graceful shutdown — stubbed in Phase 4):
1. Deregister tick callback (`MtcTickSource::setTickCallback({})`).
2. Call `FadeRegistry::cancelAll()` (no final OSC values sent).
3. Call `NngBusClient::stop()` (joins recv + drain + status worker threads).
4. `MtcTickSource` destructor closes MIDI port.

**Returns**: void.

**Throws**: Never.

**Postconditions**: No further ticks, OSC sends, or NNG messages are emitted after this returns.

---

## `onTick(long mtc_ms)` *(private, called from MTC callback thread)*

**Brief**: Per-tick handler registered with `MtcTickSource`.

**Behaviour**:
1. Try-acquire `NngBusClient::drain_in_progress_` flag.
2. If acquired: drain `LockFreeQueue` → `FadeRegistry::apply(cmd)` per command. Release flag.
3. Call `FadeRegistry::tick(mtc_ms)`.

**Thread**: Fires from the RtMidi MIDI callback thread. Must be lock-free and non-blocking (Principle IV).

**Throws**: Never.

---

## Error Propagation

`GradientEngine` uses return codes + logging (no exceptions). Internal errors (OscSender failure, CurveFactory unknown type) are surfaced to the Controller via NNG `fade_error` messages through the status worker. The daemon's `GradientEngineApplication` checks `initialize()` return value and exits with a non-zero code on failure.
