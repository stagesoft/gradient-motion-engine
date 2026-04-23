# Contract: `gme::osc::OscSender`

**File**: `src/osc/OscSender.h/.cpp`  
**Namespace**: `gme::osc`  
**Type**: Free functions (no class). Stateless — caller owns `lo_address`.

---

## `sendFloat`

```cpp
int sendFloat(lo_address target, const char* path, float value) noexcept;
```

**Brief**: Send a single-float OSC message to `target` at `path`.

**Parameters**:
- `target` — Pre-built liblo address handle (non-null). Lifetime owned by `ActiveFade`.
- `path` — OSC destination path string, e.g. `"/volmaster"`. Must be a valid OSC path (leading `/`). Null-terminated.
- `value` — Float value to send. Serialised as OSC type `f` (32-bit IEEE 754).

**Returns**: `0` on success. Negative liblo error code on failure (e.g., socket error). Same semantics as `lo_send`.

**Throws**: Never (`noexcept`).

**Preconditions**:
- `target` is non-null and was created with `lo_address_new` / `lo_address_new_with_proto`.
- `path` begins with `/` and contains no spaces (OSC path spec).

**Thread safety**: Safe to call from any single thread. Not re-entrant on the same `lo_address` (liblo is not thread-safe per address). Since each `ActiveFade` owns its own `lo_address`, concurrent calls on different fades are safe.

**Implementation notes**:
- Internally calls `lo_send(target, path, "f", (double)value)`.
- liblo formats the message into a stack buffer before calling `sendmsg(2)`. No heap allocation.
- On loopback UDP, `sendmsg` completes immediately (non-blocking) as long as the kernel socket buffer is not exhausted. See research.md Decision 2.

---

## `makeAddress`

```cpp
lo_address makeAddress(const std::string& host, int port) noexcept;
```

**Brief**: Create a liblo address for `host:port` using UDP.

**Parameters**:
- `host` — Hostname or IP string, e.g. `"127.0.0.1"`.
- `port` — UDP port number, e.g. `9234`.

**Returns**: A valid `lo_address` on success, `nullptr` on failure (bad host/port string).

**Throws**: Never (`noexcept`).

**Ownership**: Caller is responsible for calling `lo_address_free` when done. In `FadeRegistry`, this is done in `cancelFade`, `cancelAll`, and when an `ActiveFade` is removed.

**Note**: This is a thin wrapper over `lo_address_new(host.c_str(), std::to_string(port).c_str())`, exposing a C++-friendly signature with the correct type conversions.
