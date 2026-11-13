/*
    media-player
    Copyright (C) 2026  miyabi-no1-fan

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

// Thread-safe, optionally bounded, multi-producer/multi-consumer queue.
template <typename T>
class MPMCQueue {
    std::queue<T> queue;
    mutable std::mutex mutex;
    std::condition_variable not_empty;  // signalled when an item is available
    std::condition_variable not_full;   // signalled when space is available
    std::size_t bound = std::numeric_limits<decltype(bound)>::max();
    bool closed = false;

   public:
    MPMCQueue() = default;
    explicit MPMCQueue(std::size_t max_size) : bound(max_size) {}

    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    // Blocks while the queue is full. Returns value if the queue was closed
    std::optional<T> push(T value) {
        std::unique_lock lock(mutex);
        not_full.wait(lock, [&] { return closed || queue.size() < bound; });
        if (closed) return value;
        queue.push(std::move(value));
        lock.unlock();
        not_empty.notify_one();
        return std::nullopt;
    }

    // Non-blocking push. Returns value if full or closed.
    std::optional<T> try_push(T value) {
        {
            std::lock_guard lock(mutex);
            if (closed || queue.size() >= bound) return value;
            queue.push(std::move(value));
        }
        not_empty.notify_one();
        return std::nullopt;
    }

    // Blocks while the queue is empty. Returns std::nullopt only when the
    // queue is closed AND drained.
    std::optional<T> pop() {
        std::unique_lock lock(mutex);
        not_empty.wait(lock, [&] { return closed || !queue.empty(); });
        if (queue.empty()) return std::nullopt;
        T value = std::move(queue.front());
        queue.pop();
        lock.unlock();
        not_full.notify_one();
        return value;
    }

    // Non-blocking pop. Returns std::nullopt if empty.
    std::optional<T> try_pop() {
        std::unique_lock lock(mutex);
        if (queue.empty()) return std::nullopt;
        T value = std::move(queue.front());
        queue.pop();
        lock.unlock();
        not_full.notify_one();
        return value;
    }

    // Wakes all waiters. Further pushes fail; pops drain remaining items.
    void close() {
        {
            std::lock_guard lock(mutex);
            closed = true;
        }
        not_empty.notify_all();
        not_full.notify_all();
    }

    bool is_closed() const {
        std::lock_guard lock(mutex);
        return closed;
    }

    std::size_t size() const {
        std::lock_guard lock(mutex);
        return queue.size();
    }

    bool empty() const {
        std::lock_guard lock(mutex);
        return queue.empty();
    }

    std::size_t capacity() const { return bound; }
};
