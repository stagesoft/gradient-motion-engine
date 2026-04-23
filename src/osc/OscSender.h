/**
 * @file OscSender.h
 * @brief Stateless liblo UDP wrapper for the `gme::osc` module.
 *
 * Free functions — no class instantiation required. All OSC target address
 * lifetime is owned by the **caller** (in practice, by `ActiveFade`), not by
 * this module. This keeps `OscSender` free of mutable state and makes it safe
 * to call from any thread.
 *
 * ## Real-time safety
 *
 * `sendFloat` is safe to call from the MTC tick thread under the documented
 * loopback assumption (see research.md Decision 2):
 *  - `lo_send` calls `sendmsg(2)` to a loopback UDP socket.
 *  - At 50 concurrent fades × 60 bytes × 200 Hz ≈ 600 KB/s — far below
 *    loopback capacity. The syscall typically completes in 1–5 µs.
 *  - UDP is fire-and-forget: `sendmsg` does NOT block on a slow/absent receiver.
 *
 * @warning If a remote (non-loopback) OSC target is ever introduced,
 * `sendFloat` must be moved off the tick thread. Document any such change
 * in `research.md` and re-validate Principle IV.
 *
 * @par Example:
 * @code
 *   lo_address addr = gme::osc::makeAddress("127.0.0.1", 9234);
 *   if (!addr) { // handle failure }
 *
 *   int rc = gme::osc::sendFloat(addr, "/volmaster", 0.75f);
 *   if (rc != 0) { // lo error }
 *
 *   lo_address_free(addr);  // caller owns lifetime
 * @endcode
 */

#pragma once

#include <lo/lo.h>
#include <string>

namespace gme {
namespace osc {

/**
 * @brief Send a single-float OSC message to a pre-built target address.
 *
 * @param target  Non-null liblo address handle. Lifetime owned by the caller.
 *                Created via `makeAddress` or `lo_address_new`.
 * @param path    Null-terminated OSC path beginning with `/`. Must not contain
 *                spaces (OSC path spec).
 * @param value   Float value to send. Serialised as OSC type `f`
 *                (32-bit IEEE 754, passed to liblo as `double`).
 *
 * @return 0 on success. Negative liblo errno code on failure
 *         (same semantics as `lo_send`).
 *
 * @throws Never (`noexcept`).
 *
 * @note Thread safety: safe from any single thread. Not re-entrant on the
 *       same `lo_address` (liblo is not thread-safe per address). Since each
 *       `ActiveFade` owns a distinct `lo_address`, concurrent calls on
 *       different fades are safe.
 *
 * @par Example:
 * @code
 *   lo_address addr = gme::osc::makeAddress("127.0.0.1", 9000);
 *   int rc = gme::osc::sendFloat(addr, "/gain", 0.5f);
 *   lo_address_free(addr);
 * @endcode
 */
int sendFloat(lo_address target, const char* path, float value) noexcept;

/**
 * @brief Create a liblo UDP address for `host:port`.
 *
 * Thin wrapper over `lo_address_new(host.c_str(), std::to_string(port).c_str())`
 * with a C++-friendly signature.
 *
 * @param host  Hostname or IP string, e.g. `"127.0.0.1"`. Null-terminated.
 * @param port  UDP port number (1–65535).
 *
 * @return Valid `lo_address` on success. `nullptr` if `lo_address_new` fails
 *         (bad host/port string or system resource exhaustion).
 *
 * @throws Never (`noexcept`).
 *
 * @note **Ownership**: caller is responsible for `lo_address_free` when done.
 *       In `FadeRegistry`, this is done inside `cancelFade`, `cancelAll`,
 *       and the destructor path for completed/errored fades.
 *
 * @par Example:
 * @code
 *   lo_address addr = gme::osc::makeAddress("127.0.0.1", 9234);
 *   if (!addr) { return; }  // log and reject fade
 * @endcode
 */
lo_address makeAddress(const std::string& host, int port) noexcept;

} // namespace osc
} // namespace gme
