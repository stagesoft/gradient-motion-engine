/**
 * @file NngBusClient.cpp
 * @brief NNG bus0 client implementation.
 */

#include <nng/nng.h>
#include <nng/protocol/bus0/bus.h>

#include "daemon/comms/NngBusClient.h"
#include "daemon/logging.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>

namespace gme {
namespace daemon {
namespace comms {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

NngBusClient::NngBusClient(std::string nodeName,
                           gme::signal::LockFreeQueue<gme::signal::FadeCommand, 64>& queue)
    : nodeName_(std::move(nodeName))
    , senderId_("gradientengine_" + nodeName_)
    , queue_(queue)
{
}

NngBusClient::~NngBusClient() {
    stop();
}

// ---------------------------------------------------------------------------
// start / stop (T015)
// ---------------------------------------------------------------------------

StartError NngBusClient::start(const std::string& url,
                                std::function<void(gme::signal::FadeCommand&)> drainCallback) {
    drainCallback_ = std::move(drainCallback);

    sock_ = new nng_socket();

    int rv = nng_bus0_open(sock_);
    if (rv != 0) {
        GME_LOG_ERROR("NngBusClient: nng_bus0_open failed: " + std::string(nng_strerror(rv)));
        delete sock_;
        sock_ = nullptr;
        return StartError::SocketOpenFailed;
    }

    // Set reconnect bounds before dialing
    nng_socket_set_ms(*sock_, NNG_OPT_RECONNMINT, 500);
    nng_socket_set_ms(*sock_, NNG_OPT_RECONNMAXT, 5000);

    rv = nng_dial(*sock_, url.c_str(), nullptr, NNG_FLAG_NONBLOCK);
    if (rv != 0) {
        GME_LOG_ERROR("NngBusClient: nng_dial failed: " + std::string(nng_strerror(rv)));
        nng_close(*sock_);
        delete sock_;
        sock_ = nullptr;
        return StartError::DialFailed;
    }

    running_.store(true);
    recvThread_          = std::thread(&NngBusClient::recvLoop, this);
    fallbackThread_      = std::thread(&NngBusClient::fallbackDrainLoop, this);
    statusWorkerThread_  = std::thread(&NngBusClient::statusWorkerLoop, this);

    GME_LOG_INFO("NngBusClient: started, url=" + url + ", sender=" + senderId_);
    return StartError::Ok;
}

void NngBusClient::stop() {
    if (!running_.exchange(false)) return;

    // Wake fallback drain thread and status worker
    {
        std::lock_guard<std::mutex> lk(drainCv_mutex_);
        drainCv_.notify_all();
    }
    {
        std::lock_guard<std::mutex> lk(statusWorkerCv_mutex_);
        statusWorkerCv_.notify_all();
    }

    // Close socket — unblocks any in-flight nng_recv
    if (sock_) {
        nng_close(*sock_);
        delete sock_;
        sock_ = nullptr;
    }

    if (recvThread_.joinable())          recvThread_.join();
    if (fallbackThread_.joinable())      fallbackThread_.join();
    if (statusWorkerThread_.joinable())  statusWorkerThread_.join();

    connected_.store(false);
    GME_LOG_INFO("NngBusClient: stopped");
}

// ---------------------------------------------------------------------------
// recvLoop (T016)
// ---------------------------------------------------------------------------

void NngBusClient::recvLoop() {
    using gme::signal::parseFadeCommand;
    using gme::signal::classifyParseOutcome;
    using gme::signal::ParseResult;
    using gme::signal::ParseOutcomeAction;
    using gme::signal::FadeCommand;

    while (running_.load()) {
        void*  buf = nullptr;
        size_t sz  = 0;

        int rv = nng_recv(*sock_, &buf, &sz, NNG_FLAG_ALLOC);

        if (rv == NNG_EAGAIN || rv == static_cast<int>(NNG_ETIMEDOUT)) {
            continue;
        }

        if (rv != 0) {
            if (!running_.load()) break;
            // Transient error (connection lost, peer reset) — NNG dialer will
            // reconnect automatically. Stay in the loop.
            GME_LOG_WARNING("NngBusClient: nng_recv error: " + std::string(nng_strerror(rv)));
            connected_.store(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        connected_.store(true);

        // Parse JSON envelope
        nlohmann::json env = nlohmann::json::parse(
            static_cast<const char*>(buf), static_cast<const char*>(buf) + sz,
            nullptr, /*allow_exceptions=*/false);

        FadeCommand cmd;
        ParseResult r = parseFadeCommand(env, nodeName_, cmd);

        switch (classifyParseOutcome(r, !cmd.fade_id.empty())) {
            case ParseOutcomeAction::Enqueue:
                if (!queue_.push(std::move(cmd)))
                    GME_LOG_WARNING("NngBusClient: queue overflow — oldest command dropped");
                break;
            case ParseOutcomeAction::DropSilent:
                break;
            case ParseOutcomeAction::LogOnly:
                GME_LOG_WARNING("NngBusClient: parse rejected (no fade_id context)");
                break;
            case ParseOutcomeAction::LogAndStatus:
                GME_LOG_WARNING("NngBusClient: parse error for fade_id=" + cmd.fade_id);
                sendStatus(StatusKind::MotionError, cmd.fade_id, "parse_error");
                break;
        }

        nng_free(buf, sz);
    }
}

// ---------------------------------------------------------------------------
// fallbackDrainLoop (T025)
// ---------------------------------------------------------------------------

void NngBusClient::fallbackDrainLoop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lk(drainCv_mutex_);
        drainCv_.wait_for(lk, std::chrono::milliseconds(100));
        lk.unlock();

        if (!running_.load()) break;
        drainOnce(drainCallback_);
    }
}

// ---------------------------------------------------------------------------
// drainOnce (T023)
// ---------------------------------------------------------------------------

void NngBusClient::drainOnce(const std::function<void(gme::signal::FadeCommand&)>& cb) {
    if (!cb) return;

    // Try to acquire the drain lock — if another site is draining, return immediately
    if (drain_in_progress_.test_and_set(std::memory_order_acquire)) return;

    gme::signal::FadeCommand item;
    while (queue_.pop(item)) {
        cb(item);
    }

    drain_in_progress_.clear(std::memory_order_release);
}

// ---------------------------------------------------------------------------
// sendStatus (T017)
// ---------------------------------------------------------------------------

void NngBusClient::sendStatus(StatusKind kind,
                               const std::string& fadeId,
                               const std::string& reason) {
    if (!sock_) return;

    const char* event = (kind == StatusKind::MotionComplete) ? "motion_complete" : "motion_error";

    nlohmann::json data = {
        {"event",     event},
        {"fade_id",   fadeId},
        {"node_name", nodeName_}
    };
    if (!reason.empty()) {
        data["reason"] = reason;
    }

    nlohmann::json env = {
        {"type",   "status"},
        {"action", "update"},
        {"sender", senderId_},
        {"target", "gradientengine"},
        {"data",   data}
    };

    std::string payload = env.dump();
    int rv = nng_send(*sock_,
                      const_cast<char*>(payload.data()),
                      payload.size(),
                      0);
    if (rv != 0) {
        GME_LOG_WARNING("NngBusClient: sendStatus failed: " + std::string(nng_strerror(rv)));
    }
}

// ---------------------------------------------------------------------------
// pushStatus — non-blocking, safe from MTC tick thread (FR-006b)
// ---------------------------------------------------------------------------

void NngBusClient::pushStatus(gme::signal::StatusEmitRequest&& req) {
    if (!statusQueue_.push(std::move(req))) {
        GME_LOG_WARNING("NngBusClient: status queue overflow — oldest status dropped");
    }
    // Wake the status worker so it drains promptly (best-effort, non-blocking).
    // Use try_lock to avoid blocking the tick thread if the mutex is contended.
    if (statusWorkerCv_mutex_.try_lock()) {
        statusWorkerCv_.notify_one();
        statusWorkerCv_mutex_.unlock();
    }
}

// ---------------------------------------------------------------------------
// statusWorkerLoop — pops StatusEmitRequests and calls sendStatus
// ---------------------------------------------------------------------------

void NngBusClient::statusWorkerLoop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lk(statusWorkerCv_mutex_);
        // Wait up to 50 ms so we still drain promptly even without a notify
        statusWorkerCv_.wait_for(lk, std::chrono::milliseconds(50));
        lk.unlock();

        if (!running_.load()) break;

        gme::signal::StatusEmitRequest req;
        while (statusQueue_.pop(req)) {
            sendStatus(req.kind, req.fade_id, req.reason);
        }
    }

    // Drain any residual status requests before exiting
    gme::signal::StatusEmitRequest req;
    while (statusQueue_.pop(req)) {
        sendStatus(req.kind, req.fade_id, req.reason);
    }
}

// ---------------------------------------------------------------------------
// isConnected
// ---------------------------------------------------------------------------

bool NngBusClient::isConnected() const noexcept {
    return connected_.load();
}

} // namespace comms
} // namespace daemon
} // namespace gme
