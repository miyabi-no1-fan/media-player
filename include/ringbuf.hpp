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
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace RingBuf {

static constexpr std::chrono::microseconds POLLING_INTERVAL = std::chrono::microseconds(100);

enum class Error {
    EndOfInput,
    Timeout,
    Empty,
};

template <typename T>
class Receiver {
    std::shared_ptr<std::vector<T>> buf = nullptr;
    std::shared_ptr<std::atomic<std::size_t>> msg_count = nullptr;
    std::shared_ptr<std::atomic_bool> end = nullptr;
    std::size_t index = 0;

   public:
    Receiver() {}

    Receiver(std::shared_ptr<std::vector<T>> buf,
             std::shared_ptr<std::atomic<std::size_t>> msg_count,
             std::shared_ptr<std::atomic_bool> end)
        : buf(std::move(buf)), msg_count(std::move(msg_count)), end(std::move(end)) {}

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) = default;
    Receiver& operator=(Receiver&&) = default;

    std::expected<T, Error> try_receive() {
        // need to be loaded before `msg_count`
        bool isend = end->load(std::memory_order::seq_cst);

        if (msg_count->load(std::memory_order::acquire) == 0) {
            if (isend) return std::unexpected(Error::EndOfInput);
            return std::unexpected(Error::Empty);
        }

        T res = std::move((*buf)[index]);
        index = (index + 1) % buf->size();
        msg_count->fetch_sub(1, std::memory_order::acq_rel);
        return res;
    }

    template <typename D>
    std::expected<T, Error> receive_timeout(D timeout) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            auto msg = try_receive();
            if (msg || msg.error() != Error::Empty) return msg;
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }
        return std::unexpected(Error::Timeout);
    }

    template <typename D>
    std::expected<T, Error> receive_deadline(D deadline) {
        while (std::chrono::steady_clock::now() < deadline) {
            auto msg = try_receive();
            if (msg || msg.error() != Error::Empty) return msg;
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }
        return std::unexpected(Error::Timeout);
    }

    std::expected<T, Error> receive() {
        while (1) {
            auto msg = try_receive();
            if (msg || msg.error() != Error::Empty) return msg;
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }
    }

    std::size_t len() const {
        return msg_count->load(std::memory_order::acquire);
    }
};

// Sender destructor **will not** `drop_connection`
template <typename T>
class Sender {
    std::shared_ptr<std::vector<T>> buf = nullptr;
    std::shared_ptr<std::atomic<std::size_t>> msg_count = nullptr;
    std::shared_ptr<std::atomic_bool> end = nullptr;
    std::size_t index = 0;

   public:
    Sender() {}
    Sender(std::shared_ptr<std::vector<T>> buf,
           std::shared_ptr<std::atomic<std::size_t>> msg_count,
           std::shared_ptr<std::atomic_bool> end)
        : buf(std::move(buf)), msg_count(std::move(msg_count)), end(std::move(end)) {}
    ~Sender() {}
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    Sender(Sender&& o) noexcept = default;
    Sender& operator=(Sender&& o) noexcept = default;

    // return message if fail
    std::optional<T> try_send(T message) {
        if (msg_count->load(std::memory_order::acquire) >= buf->size())
            return std::move(message);
        (*buf)[index] = std::move(message);
        index = (index + 1) % buf->size();
        msg_count->fetch_add(1, std::memory_order::acq_rel);
        return std::nullopt;
    }

    template <typename D>
    std::optional<T> send_timeout(T message, D timeout) {
        auto start = std::chrono::steady_clock::now();
        while (auto msg = try_send(std::move(message))) {
            if (std::chrono::steady_clock::now() - start >= timeout)
                return msg;
            std::this_thread::sleep_for(POLLING_INTERVAL);
            message = std::move(*msg);
        }
        return std::nullopt;
    }

    void send(T message) {
        while (auto msg = try_send(std::move(message))) {
            std::this_thread::sleep_for(POLLING_INTERVAL);
            message = std::move(*msg);
        }
    }

    void drop_connection() {
        buf = nullptr;
        msg_count = nullptr;
        end->store(true, std::memory_order::seq_cst);
        end = nullptr;
    }

    std::size_t len() const {
        return msg_count->load(std::memory_order::acquire);
    }
};

template <typename T>
static inline std::pair<Sender<T>, Receiver<T>> bounded_channel(std::size_t len) {
    assert(len > 0);
    std::shared_ptr<std::vector<T>> buf = std::make_shared<std::vector<T>>(len);
    std::shared_ptr<std::atomic<std::size_t>> msg_count = std::make_shared<std::atomic<std::size_t>>(0);
    std::shared_ptr<std::atomic_bool> end = std::make_shared<std::atomic_bool>(false);
    return { Sender(buf, msg_count, end), Receiver(buf, msg_count, end) };
}

}  // namespace RingBuf