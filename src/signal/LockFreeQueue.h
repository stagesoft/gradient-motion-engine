/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file LockFreeQueue.h
 * @brief Fixed-capacity single-producer / single-consumer ring buffer.
 *
 * `LockFreeQueue<T, N>` is the hand-off path from the NNG receive
 * thread (producer) to the MTC tick thread (consumer). It satisfies
 * Principle IV (Real-Time Safety) of the project constitution:
 *
 *  - **Zero heap allocation** after construction. Storage is
 *    `std::array<T, N>` in-class.
 *  - **Wait-free consumer path** — `pop` is a single acquire-load +
 *    indexed read + release-store.
 *  - **Bounded producer path** — `push` is a single release-store on
 *    the non-full path, or a single CAS on the drop-oldest path.
 *
 * ## SPSC contract
 *
 * - **Exactly one thread calls `push`** (the NNG receive thread).
 * - **Exactly one site at a time calls `pop`** — either the MTC tick
 *   callback or the 100 ms fallback drain timer, serialised via an
 *   external `std::atomic_flag` owned by the queue's owner.
 * - `size()` and `empty()` are **advisory only** and may return
 *   transiently inconsistent values.
 *
 * ## Drop-oldest on full
 *
 * When the queue is full at `push` time, the oldest entry is dropped
 * (tail advanced one slot) and the new entry is stored. `push`
 * returns `false` to signal the drop so the caller can log a warning.
 * The achievable maximum occupancy is therefore `N - 1` slots: one
 * slot is reserved to distinguish empty from full without needing a
 * third atomic.
 *
 * @tparam T  Element type. Must be move-assignable. For the fade
 *            pipeline this is `gme::signal::FadeCommand`.
 * @tparam N  Compile-time capacity. Must be ≥ 2. Production uses 64.
 *
 * @par Example usage:
 * @code
 *   gme::signal::LockFreeQueue<FadeCommand, 64> q;
 *
 *   // Producer (NNG recv thread):
 *   FadeCommand cmd = ...;
 *   if (!q.push(std::move(cmd))) {
 *       GME_LOG_WARNING("fade command queue overflow — oldest dropped");
 *   }
 *
 *   // Consumer (MTC tick or fallback drain):
 *   FadeCommand out;
 *   while (q.pop(out)) {
 *       registry.apply(out);
 *   }
 * @endcode
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <utility>

namespace gme {
namespace signal {

/**
 * @brief Fixed-capacity SPSC ring buffer with drop-oldest-on-full.
 *
 * See file-level documentation for the full contract.
 */
template <typename T, std::size_t N>
class LockFreeQueue {
    static_assert(N >= 2, "LockFreeQueue capacity must be >= 2 (one slot is reserved)");

public:
    /**
     * @brief Construct an empty queue.
     *
     * Zero heap allocation; the `std::array<T, N>` storage is embedded.
     * `head_` and `tail_` are initialised to 0.
     */
    LockFreeQueue() noexcept = default;

    // Non-copyable, non-movable (fixed in-class storage, shared atomics).
    LockFreeQueue(const LockFreeQueue&)            = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    /**
     * @brief Push an item into the queue.
     *
     * Producer-only. Not reentrant with other `push` calls.
     *
     * If the queue is currently full (occupancy `N - 1`), the oldest
     * entry is dropped: `tail_` is advanced by one slot via a
     * compare-exchange loop, then the new item is stored.
     *
     * @param item  Item to enqueue; moved into the buffer slot.
     *
     * @return `true` if the queue had free space. `false` if the
     *         oldest entry was dropped to make room — caller SHOULD
     *         log a warning (FR-008).
     *
     * @throws None.
     *
     * @par Example:
     * @code
     *   if (!q.push(std::move(cmd))) log_warning("queue overflow");
     * @endcode
     */
    bool push(T&& item) noexcept;

    /**
     * @brief Pop an item from the queue.
     *
     * Consumer-only. Not reentrant with other `pop` calls (the
     * serialisation between the MTC-tick and fallback-drain sites is
     * the owner's responsibility — see Decision 4 in research.md).
     *
     * @param out  [out] Populated with the popped item on success.
     *             Untouched on failure.
     *
     * @return `true` if an item was popped into `out`. `false` if the
     *         queue was empty.
     *
     * @throws None.
     *
     * @par Example:
     * @code
     *   FadeCommand cmd;
     *   while (q.pop(cmd)) registry.apply(cmd);
     * @endcode
     */
    bool pop(T& out) noexcept;

    /**
     * @brief Advisory current occupancy.
     *
     * @return Approximate number of unread items. May be transiently
     *         inconsistent under concurrent push/pop. Do not use for
     *         flow control.
     */
    std::size_t size() const noexcept;

    /**
     * @brief Advisory empty check.
     *
     * @return `true` if `head_ == tail_` at the moment of call.
     *         Advisory only.
     */
    bool empty() const noexcept;

    /**
     * @brief Compile-time capacity.
     *
     * @return The template parameter `N`. The usable capacity is
     *         `N - 1` (one slot reserved to distinguish empty from
     *         full).
     */
    static constexpr std::size_t capacity() noexcept { return N; }

private:
    std::array<T, N>          buffer_{};
    std::atomic<std::size_t>  head_{0};   ///< Producer write index.
    std::atomic<std::size_t>  tail_{0};   ///< Consumer read index.
    mutable std::mutex        mtx_{};
};

// ---------------------------------------------------------------------------
// Inline implementations (template — must be in the header)
// ---------------------------------------------------------------------------

template <typename T, std::size_t N>
bool LockFreeQueue<T, N>::push(T&& item) noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) % N;

    bool dropped = false;
    std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (next == tail) {
        tail_.store((tail + 1) % N, std::memory_order_relaxed);
        dropped = true;
    }

    buffer_[head] = std::move(item);
    head_.store(next, std::memory_order_relaxed);
    return !dropped;
}

template <typename T, std::size_t N>
bool LockFreeQueue<T, N>::pop(T& out) noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (tail == head) return false;

    out = std::move(buffer_[tail]);
    tail_.store((tail + 1) % N, std::memory_order_relaxed);
    return true;
}

template <typename T, std::size_t N>
std::size_t LockFreeQueue<T, N>::size() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    const std::size_t h = head_.load(std::memory_order_relaxed);
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    return (h + N - t) % N;
}

template <typename T, std::size_t N>
bool LockFreeQueue<T, N>::empty() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return head_.load(std::memory_order_relaxed) ==
           tail_.load(std::memory_order_relaxed);
}

} // namespace signal
} // namespace gme
