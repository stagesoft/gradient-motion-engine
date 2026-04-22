# Quickstart: Phase 3 — NNG Bus Client, FadeCommand & LockFreeQueue

**Feature**: 005-nng-bus-client
**Audience**: Engineer picking up the implementation branch after
`/speckit.plan` finishes.

This is a compact, end-to-end walkthrough of the implementation and
validation steps. It is NOT the task list — `/speckit.tasks` will
expand these into granular, ordered tasks.

## 1. Build dependencies

NNG is already installed on the dev host (`libnng-dev 1.10.1`) and
`nlohmann-json3-dev 3.11.3` is also present. The root `CMakeLists.txt`
already has `pkg_check_modules(NNG nng)` as an optional check
(line 31) — promote it to required when `BUILD_DAEMON=ON`.

```cmake
# In root CMakeLists.txt, inside `if(BUILD_DAEMON)`:
pkg_check_modules(NNG REQUIRED nng)
```

```cmake
# In the daemon target block:
target_include_directories(gradient-motiond PRIVATE ${NNG_INCLUDE_DIRS})
target_link_libraries(gradient-motiond PRIVATE ${NNG_LIBRARIES})
```

## 2. Add the library-side headers

Both are header-only in this phase.

```text
src/signal/FadeCommand.h       # Copy from specs/005-nng-bus-client/contracts/
src/signal/LockFreeQueue.h     # Copy from specs/005-nng-bus-client/contracts/
```

`FadeCommand.h` needs one `.cpp` companion (for `parseFadeCommand`):

```text
src/signal/FadeCommand.cpp     # Implements parseFadeCommand per data-model.md
```

Update `src/CMakeLists.txt`:

```cmake
set(GME_SOURCES
    time/time.cpp
    time/MtcTickSource.cpp
    motion/motion.cpp
    gradient/SigmoidCurve.cpp
    gradient/BezierCurve.cpp
    gradient/ResampledCurve.cpp
    gradient/CurveFactory.cpp
    signal/signal.cpp
    signal/FadeCommand.cpp       # NEW
    osc/osc.cpp
    engine/engine.cpp
)
```

`LockFreeQueue.h` is a template — no `.cpp`. The existing
`signal/signal.cpp` stays as the module's translation-unit anchor.

## 3. Add the daemon-side component

```text
daemon/comms/NngBusClient.h     # Copy from specs/005-nng-bus-client/contracts/
daemon/comms/NngBusClient.cpp   # Wire nng_bus0_open, nng_dial, recvLoop, sendStatus
```

Update the daemon target in the root `CMakeLists.txt`:

```cmake
add_executable(gradient-motiond
    daemon/main.cpp
    daemon/GradientEngineApplication.cpp
    daemon/config/ConfigurationManager.cpp
    daemon/comms/NngBusClient.cpp       # NEW
)
```

## 4. Add a `--node-name` flag to ConfigurationManager

Minimal surface: one getter, one CLI flag, hostname fallback. No XML
yet (that is Phase 5).

```cpp
// ConfigurationManager.h
const std::string& getNodeName() const { return nodeName_; }

// ConfigurationManager.cpp
// In parseArgs, add:
static struct option long_opts[] = {
    {"midi-port", required_argument, 0, 'm'},
    {"log-level", required_argument, 0, 'l'},
    {"conf-path", required_argument, 0, 'c'},
    {"node-name", required_argument, 0, 'n'},   // NEW
    ...
};
// Case 'n': nodeName_ = optarg;

// Default to hostname if not provided:
if (nodeName_.empty()) {
    char host[HOST_NAME_MAX + 1] = {0};
    if (gethostname(host, sizeof(host) - 1) == 0) {
        nodeName_ = host;
        GME_LOG_INFO("node_name defaulted to hostname: " + nodeName_);
    } else {
        GME_LOG_ERROR("gethostname failed; --node-name is required");
        return 1;
    }
}
```

## 5. Implementation skeleton — parseFadeCommand

The parser is the core of this phase. Follow the required-field matrix
in `data-model.md` and the `ParseResult` contract in
`contracts/FadeCommand.h`:

```cpp
// signal/FadeCommand.cpp
#include "signal/FadeCommand.h"

namespace gme::signal {

namespace {
bool getString(const nlohmann::json& j, const char* key, std::string& out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}
// Similar helpers for int, float, long ...
} // namespace

ParseResult parseFadeCommand(const nlohmann::json& env,
                             const std::string& ownNodeName,
                             FadeCommand& out) {
    if (env.is_discarded() || !env.is_object()) return ParseResult::MalformedJson;

    // 1. Target filter (highest-traffic short-circuit).
    auto t = env.find("target");
    if (t == env.end() || !t->is_string() || t->get<std::string>() != "gradientengine")
        return ParseResult::TargetMismatch;

    auto d = env.find("data");
    if (d == env.end() || !d->is_object()) return ParseResult::MissingField;

    // 2. Node filter.
    std::string node;
    if (!getString(*d, "node_name", node) || node != ownNodeName)
        return ParseResult::NodeMismatch;
    out.node_name = node;

    // 3. Dispatch on command.
    std::string cmd;
    if (!getString(*d, "command", cmd)) return ParseResult::MissingField;

    if (cmd == "start_fade")       return parseStartFade(*d, out);
    if (cmd == "cancel_fade")      return parseCancelFade(*d, out);
    if (cmd == "cancel_all")       { out.type = FadeCommand::Type::CANCEL_ALL; return ParseResult::Ok; }
    if (cmd == "start_crossfade")  return parseStartCrossfade(*d, out);

    return ParseResult::UnknownCommand;
}

} // namespace gme::signal
```

## 6. Implementation skeleton — NngBusClient

See `contracts/NngBusClient.h` for the declarations. The
implementation follows Decision 1 + Decision 5 in `research.md`:

```cpp
// daemon/comms/NngBusClient.cpp
#include "NngBusClient.h"
#include "logging.h"

#include <nng/nng.h>
#include <nng/protocol/bus0/bus.h>

namespace gme::daemon::comms {

using gme::signal::FadeCommand;
using gme::signal::ParseResult;

NngBusClient::NngBusClient(std::string nodeName,
                           gme::signal::LockFreeQueue<FadeCommand, 64>& queue)
    : nodeName_(std::move(nodeName))
    , senderId_("gradientengine_" + nodeName_)
    , queue_(queue) {}

NngBusClient::~NngBusClient() { stop(); }

StartError NngBusClient::start(const std::string& url) {
    ::nng_socket s;
    if (::nng_bus0_open(&s) != 0) return StartError::SocketOpenFailed;
    ::nng_socket_set_ms(s, NNG_OPT_RECONNMINT, 1000);
    ::nng_socket_set_ms(s, NNG_OPT_RECONNMAXT, 30000);
    if (::nng_dial(s, url.c_str(), nullptr, NNG_FLAG_NONBLOCK) != 0) {
        ::nng_close(s);
        return StartError::DialFailed;
    }
    sock_ = reinterpret_cast<nng_socket*>(/* stash s.id */);
    running_ = true;
    recvThread_ = std::thread(&NngBusClient::recvLoop, this);
    return StartError::Ok;
}

void NngBusClient::recvLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        void* buf = nullptr;
        size_t sz = 0;
        int rv = ::nng_recv(/* s */, &buf, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) continue;   // timeout / transient — retry
        auto env = nlohmann::json::parse(
            static_cast<const char*>(buf),
            static_cast<const char*>(buf) + sz,
            nullptr, /*allow_exceptions=*/false);
        ::nng_free(buf, sz);

        FadeCommand cmd;
        switch (parseFadeCommand(env, nodeName_, cmd)) {
            case ParseResult::Ok:
                if (!queue_.push(std::move(cmd)))
                    GME_LOG_WARNING("fade command queue overflow — oldest dropped");
                break;
            case ParseResult::MissingField:
            case ParseResult::TypeError:
                if (!cmd.fade_id.empty())
                    sendStatus(StatusKind::FadeError, cmd.fade_id, "parse_error");
                break;
            default: break;
        }
    }
}

} // namespace gme::daemon::comms
```

The actual socket-id storage needs a real `nng_socket` value type (the
forward-declared `struct nng_socket_s` in the header is a placeholder
to avoid pulling `<nng/nng.h>` into library-visible headers). In the
`.cpp`, either wrap `nng_socket` in a PIMPL or store the `uint32_t id`
field directly. PIMPL is preferable — see the `NngSocketImpl` helper
in Phase 4's integration harness.

## 7. Tests

### test_lockfree_queue.cpp (library, no NNG)

Follow the pattern in `tests/test_curves.cpp` (hand-rolled `CHECK`
macros). Coverage required:

- Single-thread: push/pop preserves FIFO order across 1000 items.
- Drop-oldest: push N items into an `N`-capacity queue, verify the
  first item has been dropped and the queue now holds the latest
  `N - 1`.
- SPSC stress: producer thread pushes 10 000 monotonically-numbered
  items; consumer thread pops until shutdown. Verify no duplicates,
  no gaps beyond the documented drop policy. Run under TSan in the
  `build-tsan/` tree to catch race regressions.

### test_nng_parse.cpp (library + parser, no NNG socket)

- Valid `start_fade` JSON → `ParseResult::Ok`, all 10 fields set
  correctly.
- `target: "nodeengine"` → `TargetMismatch`, drop silently.
- `data.node_name: "nodeB"` with `ownNodeName = "nodeA"` →
  `NodeMismatch`.
- Missing `data.fade_id` on `start_fade` → `MissingField`.
- `data.duration_ms` as string → `TypeError`, `out.fade_id` populated
  if that field was present.
- Unknown top-level `data.foo` → ignored; `ParseResult::Ok`.
- Unknown `curve_params.newKnob` → preserved in
  `out.curve_params` (round-trip through `dump()`).
- `cancel_fade` minimal payload → `Ok`, `type == CANCEL_FADE`.
- `cancel_all` → `Ok`, `type == CANCEL_ALL`, `fade_id` empty.
- `start_crossfade` → `Ok`, all primary + partner fields set;
  shared `duration_ms` and `curve_type` applied to both sides
  (the struct stores them once; consumers share them).

Both test binaries go in `tests/CMakeLists.txt`:

```cmake
add_executable(test_lockfree_queue test_lockfree_queue.cpp)
target_link_libraries(test_lockfree_queue PRIVATE gradient_motion)
add_test(NAME lockfree_queue COMMAND test_lockfree_queue)

add_executable(test_nng_parse test_nng_parse.cpp)
target_link_libraries(test_nng_parse PRIVATE gradient_motion)
add_test(NAME nng_parse COMMAND test_nng_parse)
```

No NNG link needed — the tests exercise the parse function and the
queue in isolation.

## 8. Build & run

```bash
# Normal build
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure

# TSan build (already configured in build-tsan/)
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

Both builds must pass the new tests. TSan is the primary validator
for Decision 3's atomic ordering claims.

## 9. What this phase does NOT do

- **No wiring into `GradientEngineApplication`** — Decision 7 explicitly
  defers that to the first Phase 4 task.
- **No real NNG loopback test** — the parse + queue tests cover the
  library-level behaviour. Socket-level validation happens in Phase 4.
- **No XML config parsing** — only the `--node-name` CLI flag is
  added.
- **No `FadeRegistry`, `ActiveFade`, `OscSender`** — those are Phase 4.
- **No start_fade fades actually run** — this phase only delivers the
  ingest pipeline.

## 10. Smoke-test against a real Controller (optional, end-of-phase)

Once wired (first Phase 4 task), a smoke test:

```bash
# Start the daemon
./build/gradient-motiond --node-name nodeA --log-level debug &

# From another shell, use pynng to send a well-formed fade command
python3 -c "
import json, pynng
with pynng.Bus0(dial='tcp://127.0.0.1:9093') as s:
    s.send(json.dumps({
        'type':'command','action':'update',
        'sender':'controller','target':'gradientengine',
        'data':{
            'command':'start_fade','fade_id':'smoke1','node_name':'nodeA',
            'osc_host':'127.0.0.1','osc_port':9234,'osc_path':'/volmaster',
            'start_value':0.0,'end_value':1.0,'duration_ms':3000,
            'curve_type':'linear','start_mtc_ms':-1}}).encode())
"
```

Expected daemon log:

```
DEBUG  NngBusClient  recv 212 bytes
DEBUG  NngBusClient  parseFadeCommand → Ok  fade_id=smoke1
DEBUG  NngBusClient  enqueued FadeCommand (queue size=1)
```

The consumer side (tick application, OSC send) is Phase 4 — no
audible fade yet.
