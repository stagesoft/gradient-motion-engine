<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# API synopsis

This page lists the public C++ surface of `libgradient_motion` and the `gradient-motiond` daemon. The generated API reference is published at [stagesoft.github.io/gradient-motion-engine](https://stagesoft.github.io/gradient-motion-engine/), built from the docstrings in the headers themselves.

Every namespace below is rooted at `gme::`. The header file for each entry is linked.

---

## `gme::time`

[`src/time/MtcTickSource.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/time/MtcTickSource.h)

```cpp
namespace gme::time {

enum class MtcStartError { kOk, kNoPortsAvailable, kPortNotFound };

class MtcTickSource {
public:
    MtcTickSource();
    ~MtcTickSource();   // blocks until any in-flight callback returns

    void          setTickCallback(std::function<void(long)> cb);
    MtcStartError start(const std::string& midiPort);
    long          getMtcMs() const;
    bool          isRunning() const;
};

} // namespace gme::time
```

One instance per process — `mtcreceiver` uses process-global static state.

---

## `gme::gradient`

[`src/gradient/Curve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/Curve.h) — abstract base:

```cpp
namespace gme::gradient {

class Curve {
public:
    virtual double evaluate(double t) const = 0;
    virtual ~Curve() = default;
    Curve(const Curve&)            = delete;
    Curve& operator=(const Curve&) = delete;
    Curve(Curve&&)            = default;
    Curve& operator=(Curve&&) = default;
};

} // namespace gme::gradient
```

Concrete curves:

| Header | Class | Parameters |
|---|---|---|
| [`LinearCurve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/LinearCurve.h) | `LinearCurve` | — |
| [`SigmoidCurve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/SigmoidCurve.h) | `SigmoidCurve` | `steepness` (default 8.0), `midpoint` (default 0.5) |
| [`BezierCurve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/BezierCurve.h) | `BezierCurve` | `cx1`, `cy1`, `cx2`, `cy2` (defaults `0.25, 0.1, 0.75, 0.9`) |
| [`EaseInCurve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/EaseInCurve.h) | `EaseInCurve` | `exponent` (default 2.0) |
| [`EaseOutCurve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/EaseOutCurve.h) | `EaseOutCurve` | `exponent` (default 2.0) |
| [`SCurve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/SCurve.h) | `SCurve` | — |
| [`ScaledCurve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/ScaledCurve.h) | `ScaledCurve` | output min/max scaling |
| [`ResampledCurve.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/ResampledCurve.h) | `ResampledCurve` | inner curve + 256-sample LUT |
| [`CrossfadePair.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/CrossfadePair.h) | `CrossfadePair` | two curves (deferred motion type) |

[`src/gradient/CurveFactory.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/gradient/CurveFactory.h):

```cpp
namespace gme::gradient {

class CurveFactory {
public:
    static std::optional<std::unique_ptr<Curve>>
    createCurve(const std::string& type,
                const nlohmann::json& params = nlohmann::json::object());

private:
    CurveFactory() = delete;
};

} // namespace gme::gradient
```

Supported `type` strings: `"linear"`, `"sigmoid"`, `"bezier"`, `"ease_in"`, `"ease_out"`, `"scurve"`. Unknown types return `std::nullopt`. The returned curve is always wrapped in a `ResampledCurve`.

---

## `gme::motion`

[`src/motion/IMotion.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/motion/IMotion.h):

```cpp
namespace gme::motion {

class IMotion {
public:
    std::string motion_id;
    std::string osc_key;              // "host:port:path"
    long        start_mtc_ms = 0;
    float       duration_ms = 0.0f;
    bool        completed = false;
    int         consecutive_osc_failures = 0;

    virtual ~IMotion() = default;
    virtual EvalResult evalAndSend(long mtc_ms) = 0;
    virtual void       sendSnapToEnd() = 0;
    virtual void       inheritFrom(const IMotion& prior) = 0;
};

} // namespace gme::motion
```

[`src/motion/EvalResult.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/motion/EvalResult.h):

```cpp
struct EvalResult {
    bool        completed      = false;
    bool        failed         = false;
    const char* failure_reason = nullptr;   // static storage
};
```

[`src/motion/FadeMotion.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/motion/FadeMotion.h):

```cpp
class FadeMotion final : public IMotion {
public:
    using OscSendFn = std::function<int(lo_address, const char*, float)>;

    FadeMotion(std::string motion_id,
               std::string osc_key,
               long  start_mtc_ms,
               float duration_ms,
               float start_value,
               float end_value,
               std::unique_ptr<gme::gradient::Curve> curve,
               lo_address osc_target,           // takes ownership
               std::string osc_path,
               std::string osc_host,
               int   osc_port,
               OscSendFn oscSend);
    ~FadeMotion() override;                     // lo_address_free(osc_target_)

    EvalResult evalAndSend(long mtc_ms) override;
    void       sendSnapToEnd() override;
    void       inheritFrom(const IMotion& prior) override;

    float start_value     = 0.0f;
    float end_value       = 0.0f;
    float last_sent_value = 0.0f;
    std::string osc_path;
    std::string osc_host;
    int         osc_port = 0;
};
```

[`src/motion/MotionFactory.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/motion/MotionFactory.h):

```cpp
class MotionFactory {
public:
    using OscSendFn = std::function<int(lo_address, const char*, float)>;

    struct Context {
        const gme::time::MtcTickSource& mtcSource;
        OscSendFn                       oscSend;
        std::function<void(gme::signal::StatusKind,
                           const std::string&,
                           const std::string&)> emitStatus;
    };

    static std::unique_ptr<IMotion> fromCommand(const gme::signal::FadeCommand& cmd,
                                                const Context& ctx);
    MotionFactory() = delete;
};
```

[`src/motion/MotionRegistry.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/motion/MotionRegistry.h):

```cpp
class MotionRegistry {
public:
    static constexpr int kOscFailureThreshold = 5;

    using OscSendFn = std::function<int(lo_address, const char*, float)>;

    MotionRegistry(const gme::time::MtcTickSource& mtcSource,
                   std::function<void(gme::signal::StatusKind,
                                     const std::string&,
                                     const std::string&)> statusDirect,
                   OscSendFn oscSend = nullptr);

    void   apply(gme::signal::FadeCommand& cmd);
    void   addMotion(std::unique_ptr<IMotion> m);
    void   cancelMotion(const std::string& motion_id, bool snap_to_end);
    void   cancelAll();
    void   tick(long mtc_ms);
    std::size_t size() const noexcept;
};
```

---

## `gme::signal`

[`src/signal/FadeCommand.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/signal/FadeCommand.h):

```cpp
namespace gme::signal {

struct FadeCommand {
    enum class Type { START_FADE, CANCEL_MOTION, CANCEL_ALL, START_CROSSFADE };

    Type type = Type::START_FADE;

    std::string motion_id;
    std::string node_name;

    std::string osc_host;
    int         osc_port = 0;
    std::string osc_path;

    float start_value  = 0.0f;
    float end_value    = 0.0f;
    float duration_ms  = 0.0f;

    std::string    curve_type;
    nlohmann::json curve_params;

    long start_mtc_ms = -1;   // -1 = "start at current MTC"

    std::string partner_motion_id;
    std::string partner_osc_path;
    float       partner_start_value = 0.0f;
    float       partner_end_value   = 0.0f;
};

enum class ParseResult {
    Ok, TargetMismatch, NodeMismatch, UnknownCommand,
    MissingField, TypeError, MalformedJson
};

enum class StatusKind { MotionComplete, MotionError };

} // namespace gme::signal
```

Required field matrix:

| Command          | Required                                                                  |
|------------------|---------------------------------------------------------------------------|
| `start_fade`     | `motion_id`, `node_name`, `osc_host`, `osc_port`, `osc_path`, `start_value`, `end_value`, `duration_ms`, `curve_type`, `start_mtc_ms` |
| `cancel_motion`  | `motion_id`, `node_name`                                                  |
| `cancel_all`     | `node_name`                                                               |
| `start_crossfade`| `start_fade` fields + `partner_motion_id`, `partner_osc_path`, `partner_start_value`, `partner_end_value` |

[`src/signal/parseFadeOscCommand.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/signal/parseFadeOscCommand.h):

```cpp
namespace gme::signal {

ParseResult parseFadeOscCommand(const char* path,
                                const char* types,
                                lo_arg** argv,
                                int argc,
                                std::string_view this_node_name,
                                FadeCommand* out_cmd);

} // namespace gme::signal
```

OSC address → type-tag:

| Address                     | Type tag       |
|-----------------------------|----------------|
| `/gradient/start_fade`      | `sssisffhiss`  |
| `/gradient/cancel_motion`   | `ss`           |
| `/gradient/cancel_all`      | `s`            |

[`src/signal/LockFreeQueue.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/signal/LockFreeQueue.h):

```cpp
template <typename T, std::size_t N>
class LockFreeQueue {
public:
    LockFreeQueue() noexcept = default;

    bool         push(T&& item) noexcept;   // drop-oldest on full; false signals drop
    bool         pop(T& out) noexcept;
    std::size_t  size() const noexcept;     // advisory only
    bool         empty() const noexcept;    // advisory only
    static constexpr std::size_t capacity() noexcept { return N; }
};
```

Production instantiation: `LockFreeQueue<gme::signal::FadeCommand, 64>`. Usable capacity is `N - 1`.

---

## `gme::osc`

[`src/osc/OscSender.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/osc/OscSender.h):

```cpp
namespace gme::osc {

int        sendFloat(lo_address target, const char* path, float value) noexcept;
lo_address makeAddress(const std::string& host, int port) noexcept;

} // namespace gme::osc
```

Caller owns the `lo_address` returned by `makeAddress` and must call `lo_address_free` on destruction. In production, `FadeMotion`'s destructor does this.

---

## `gme::engine`

[`src/engine/GradientEngine.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/src/engine/GradientEngine.h):

```cpp
namespace gme::engine {

struct GradientEngineConfig {
    std::string midiPort;
    int         oscPort;
    std::string nodeName;
};

class GradientEngine {
public:
    GradientEngine();
    ~GradientEngine();   // calls shutdown() if still running

    bool initialize(const GradientEngineConfig& config);
    void shutdown();     // idempotent
};

} // namespace gme::engine
```

`OscServer` is forward-declared in this header; the destructor body lives in `GradientEngine.cpp`, which is compiled into the daemon binary rather than the library.

---

## `gme::daemon::comms`

[`daemon/comms/OscServer.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/daemon/comms/OscServer.h):

```cpp
namespace gme::daemon::comms {

class OscServer {
public:
    OscServer(int port,
              std::string node_name,
              gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>* out_queue);
    ~OscServer();

    bool start();   // returns false on bind failure
    void stop();    // idempotent
    int  getPort() const noexcept;
};

} // namespace gme::daemon::comms
```

PIMPL pattern — the liblo headers are not exposed to consumers of this header. Binds `127.0.0.1` only.

---

## Daemon-layer classes (no namespace)

[`daemon/GradientEngineApplication.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/daemon/GradientEngineApplication.h):

```cpp
class GradientEngineApplication {
public:
    GradientEngineApplication();
    ~GradientEngineApplication();

    int  initialize(int argc, char** argv);   // 0 = run; <0 = clean exit; >0 = error
    int  run();                                // blocks until SIGTERM/SIGINT
    void shutdown();
};
```

[`daemon/config/ConfigurationManager.h`](https://github.com/stagesoft/gradient-motion-engine/blob/main/daemon/config/ConfigurationManager.h):

```cpp
class ConfigurationManager {
public:
    ConfigurationManager();
    ~ConfigurationManager() = default;

    int parseArgs(int argc, char** argv);   // 0 = ok, -1 = help/version, >0 = error

    const std::string& getMidiPort()         const;
    const std::string& getLogLevel()         const;
    const std::string& getConfPath()         const;
    int                getGradientOscPort()  const;
    const std::string& getNodeName()         const;
};
```

CLI flags and resolution order are documented in the header's Doxygen block. OSC-port priority: `--osc-port` → `CUEMS_GRADIENT_OSC_PORT` → `settings.xml` `<gradient_osc_port>` → compile-time default `7100`.
