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
    std::shared_ptr<std::vector<std::optional<T>>> buf = nullptr;
    std::shared_ptr<std::atomic<std::size_t>> msg_count = nullptr;
    std::size_t index = 0;

   public:
    Receiver() {}

    Receiver(std::shared_ptr<std::vector<std::optional<T>>> buf,
             std::shared_ptr<std::atomic<std::size_t>> msg_count)
        : buf(std::move(buf)), msg_count(std::move(msg_count)) {}

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) = default;
    Receiver& operator=(Receiver&&) = default;

    std::expected<T, Error> try_receive() {
        if (msg_count->load(std::memory_order::acquire) == 0)
            return std::unexpected(Error::Empty);

        std::optional<T> res = std::move((*buf)[index]);
        if (!res) return std::unexpected(Error::EndOfInput);

        index = (index + 1) % buf->size();
        msg_count->fetch_sub(1, std::memory_order::acq_rel);
        return std::move(*res);
    }

    template <typename D>
    std::expected<T, Error> receive_timeout(D timeout) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            auto msg = try_receive();
            if (msg != std::unexpected(Error::Empty)) return msg;
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }
        return std::unexpected(Error::Timeout);
    }

    template <typename D>
    std::expected<T, Error> receive_deadline(D deadline) {
        while (std::chrono::steady_clock::now() < deadline) {
            auto msg = try_receive();
            if (msg != std::unexpected(Error::Empty)) return msg;
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }
        return std::unexpected(Error::Timeout);
    }

    std::expected<T, Error> receive() {
        while (1) {
            auto msg = try_receive();
            if (msg != std::unexpected(Error::Empty)) return msg;
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }
    }

    std::size_t len() const {
        return msg_count->load(std::memory_order::acquire);
    }
};

// Sender destructor **will not** `send_eof`
template <typename T>
class Sender {
    std::shared_ptr<std::vector<std::optional<T>>> buf = nullptr;
    std::shared_ptr<std::atomic<std::size_t>> msg_count = nullptr;
    std::size_t index = 0;
    bool closed = false;

   public:
    Sender() {}

    Sender(std::shared_ptr<std::vector<std::optional<T>>> buf,
           std::shared_ptr<std::atomic<std::size_t>> msg_count)
        : buf(std::move(buf)), msg_count(std::move(msg_count)) {}

    ~Sender() {}

    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    Sender(Sender&& o) noexcept
        : buf(std::move(o.buf)),
          msg_count(std::move(o.msg_count)),
          index(std::exchange(o.index, 0)),
          closed(std::exchange(o.closed, false)) {}
    Sender& operator=(Sender&& o) noexcept {
        if (this != &o) {
            send_eof();
            buf = std::move(o.buf);
            msg_count = std::move(o.msg_count);
            index = std::exchange(o.index, 0);
            closed = std::exchange(o.closed, false);
        }
        return *this;
    }

    // return message if fail
    std::optional<T> try_send(T message) {
        {
            auto count = msg_count->load(std::memory_order::acquire);
            assert(count <= buf->size());
            if (count == buf->size())
                return std::move(message);
        }
        (*buf)[index] = std::move(message);
        index = (index + 1) % buf->size();
        msg_count->fetch_add(1, std::memory_order::acq_rel);
        return std::nullopt;
    }

    bool try_send_eof() {
        if (!buf) return true;
        if (closed) return true;

        auto count = msg_count->load(std::memory_order::acquire);
        assert(count <= buf->size());
        if (count == buf->size())
            return false;

        (*buf)[index] = std::nullopt;
        msg_count->fetch_add(1, std::memory_order::acq_rel);

        return closed = true;
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

    void send_eof() {
        while (!try_send_eof()) {
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }
    }

    std::size_t len() const {
        return msg_count->load(std::memory_order::acquire);
    }
};

template <typename T>
static inline std::pair<Sender<T>, Receiver<T>> bounded_channel(std::size_t len) {
    assert(len > 0);
    std::shared_ptr<std::vector<std::optional<T>>> buf = std::make_shared<std::vector<std::optional<T>>>(len);
    std::shared_ptr<std::atomic<std::size_t>> msg_count = std::make_shared<std::atomic<std::size_t>>(0);
    return { Sender(buf, msg_count), Receiver(buf, msg_count) };
}

}  // namespace RingBuf