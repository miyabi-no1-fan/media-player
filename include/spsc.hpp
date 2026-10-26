#pragma once
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

namespace SPSC {

static constexpr std::chrono::microseconds POLLING_INTERVAL = std::chrono::microseconds(100);

template <typename T>
class Container {
    struct Node {
        std::unique_ptr<Node> next = nullptr;
        std::optional<T> value = std::nullopt;  // value == nullopt if dummy node or eof
        Node(T value) : value(std::move(value)) {}
        Node() {}
    };

    std::unique_ptr<Node> head = nullptr;
    Node* tail = nullptr;

   public:
    Container() {
        head = std::make_unique<Node>();  // dummy node
        tail = head.get();
    }

    ~Container() {
        while (head->next) head = std::move(head->next);
    }

    void push_back(T value) {
        tail->next = std::make_unique<Node>(std::move(value));
        tail = tail->next.get();
    }

    void push_null() {
        tail->next = std::make_unique<Node>();
        tail = tail->next.get();
    }

    std::optional<T> pop_front() {
        if (!head->next->value) return std::nullopt;  // stuck at null node
        T res = std::move(*head->next->value);
        head = std::move(head->next);
        return res;
    }
};

struct Counters {
    alignas(64) std::atomic_ulong sent = 0;
    alignas(64) std::atomic_ulong received = 0;
};

enum class Error {
    Timeout,
    EndOfInput,
    Empty,
};

template <typename T>
class Receiver {
    std::shared_ptr<Container<T>> buf = nullptr;
    std::shared_ptr<Counters> msg_count = nullptr;

   public:
    Receiver() {}

    Receiver(std::shared_ptr<Container<T>> buf,
             std::shared_ptr<Counters> msg_count)
        : buf(std::move(buf)), msg_count(std::move(msg_count)) {}

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) = default;
    Receiver& operator=(Receiver&&) = default;

    std::expected<T, Error> try_receive() {
        auto r = msg_count->received.load(std::memory_order::relaxed);
        if (msg_count->sent.load(std::memory_order::acquire) == r)
            return std::unexpected(Error::Empty);

        std::optional<T> res = buf->pop_front();
        if (!res) return std::unexpected(Error::EndOfInput);

        msg_count->received.fetch_add(1, std::memory_order::relaxed);
        return std::move(*res);
    }

    template <typename D>
    std::expected<T, Error> receive_timeout(D duration) {
        auto start = std::chrono::steady_clock::now();
        do {
            auto res = try_receive();
            if (res != std::unexpected(Error::Empty)) return res;
            std::this_thread::sleep_for(POLLING_INTERVAL);
        } while (std::chrono::steady_clock::now() - start < duration);
        return std::unexpected(Error::Timeout);
    }

    std::expected<T, Error> receive() {
        while (1) {
            auto res = try_receive();
            if (res != std::unexpected(Error::Empty)) return res;
            std::this_thread::sleep_for(POLLING_INTERVAL);
        }
    }

    unsigned long len() const {
        unsigned long s = msg_count->sent.load(std::memory_order::acquire);
        unsigned long r = msg_count->received.load(std::memory_order::relaxed);
        return std::abs((long)(s - r));
    }
};

template <typename T>
class Sender {
    std::shared_ptr<Container<T>> buf = nullptr;
    std::shared_ptr<Counters> msg_count = nullptr;
    bool closed = false;

   public:
    Sender() {}

    Sender(std::shared_ptr<Container<T>> buf,
           std::shared_ptr<Counters> msg_count)
        : buf(std::move(buf)), msg_count(std::move(msg_count)) {}

    ~Sender() {
        drop_connection();
    }

    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    Sender(Sender&& o) noexcept
        : buf(std::move(o.buf)), msg_count(std::move(o.msg_count)), closed(o.closed) {}
    Sender& operator=(Sender&& o) noexcept {
        if (this != &o) {
            drop_connection();
            buf = std::move(o.buf);
            msg_count = std::move(o.msg_count);
            closed = o.closed;
        }
        return *this;
    }

    void send(T message) {
        buf->push_back(std::move(message));
        msg_count->sent.fetch_add(1, std::memory_order::release);
    }

    void drop_connection() {
        if (!buf) return;
        closed = true;
        buf->push_null();
        msg_count->sent.fetch_add(1, std::memory_order::release);
    }

    unsigned long len() const {
        unsigned long s = msg_count->sent.load(std::memory_order::relaxed);
        unsigned long r = msg_count->received.load(std::memory_order::relaxed);
        return std::abs((long)(s - r));
    }
};

template <typename T>
static inline std::pair<Sender<T>, Receiver<T>> unbounded_channel() {
    std::shared_ptr<Container<T>> buf = std::make_shared<Container<T>>();
    std::shared_ptr<Counters> msg_count = std::make_shared<Counters>();
    return { Sender(buf, msg_count), Receiver(buf, msg_count) };
}

}  // namespace SPSC