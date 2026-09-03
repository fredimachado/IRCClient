#include "IRCSocket.h"
#include "SocketOps.h"
#include "../support/LoopbackServer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace
{
class FakeSocketOps final : public SocketOps
{
public:
    handle_type create() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++create_count;
        if (reuse_handle != invalid_handle)
            live_handle_ = reuse_handle;
        else
            live_handle_ = next_handle_++;
        closed_ = false;
        shut_down_ = false;
        return live_handle_;
    }

    int connect(handle_type, std::string_view, int) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++connect_count;
        return connect_result;
    }

    int send(handle_type socket, const char* buffer, std::size_t length) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ++send_count;
        if (closed_ || socket != live_handle_)
            ++ops_after_close;
        if (buffer == nullptr)
            return -1;

        if (block_send)
        {
            send_waiting_ = true;
            waiting_cv_.notify_all();
            const bool woken = cv_.wait_for(lock, std::chrono::seconds{5}, [this] {
                return shut_down_ || closed_ || !block_send;
            });
            send_waiting_ = false;
            if (!woken || shut_down_ || closed_)
                return -1;
        }

        sent.append(buffer, length);
        return static_cast<int>(length);
    }

    int recv(handle_type socket, char* buffer, std::size_t length) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ++recv_count;
        if (closed_ || socket != live_handle_)
            ++ops_after_close;

        recv_waiting_ = true;
        waiting_cv_.notify_all();

        const bool woken = cv_.wait_for(lock, std::chrono::seconds{5}, [this] {
            return shut_down_ || closed_ || !pending_recv_.empty() || !block_recv;
        });
        recv_waiting_ = false;

        if (!woken)
            return -1;
        if (shut_down_ || closed_)
            return 0;
        if (!pending_recv_.empty())
        {
            const std::size_t n = (std::min)(length, pending_recv_.size());
            std::memcpy(buffer, pending_recv_.data(), n);
            pending_recv_.erase(0, n);
            return static_cast<int>(n);
        }
        return 0;
    }

    int shutdown(handle_type) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++shutdown_count;
        shut_down_ = true;
        cv_.notify_all();
        return 0;
    }

    int close(handle_type socket) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++close_count;
        if (socket != live_handle_ && live_handle_ != invalid_handle)
            ++ops_after_close;
        closed_ = true;
        live_handle_ = invalid_handle;
        cv_.notify_all();
        return 0;
    }

    [[nodiscard]] bool last_error_is_retryable() const override
    {
        return false;
    }

    bool wait_until_recv_waiting(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return waiting_cv_.wait_for(lock, timeout, [this] { return recv_waiting_; });
    }

    bool wait_until_send_waiting(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return waiting_cv_.wait_for(lock, timeout, [this] { return send_waiting_; });
    }

    void unblock_send()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        block_send = false;
        closed_ = true;
        shut_down_ = true;
        cv_.notify_all();
    }

    int connect_result = 0;
    handle_type reuse_handle = invalid_handle;
    bool block_recv = true;
    bool block_send = false;

    std::string sent;
    int create_count = 0;
    int connect_count = 0;
    int send_count = 0;
    int recv_count = 0;
    int shutdown_count = 0;
    int close_count = 0;
    int ops_after_close = 0;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable waiting_cv_;
    handle_type next_handle_ = 1;
    handle_type live_handle_ = invalid_handle;
    std::string pending_recv_;
    bool closed_ = false;
    bool shut_down_ = false;
    bool recv_waiting_ = false;
    bool send_waiting_ = false;
};
} // namespace

TEST_CASE("IRCSocket closes the handle exactly once")
{
    FakeSocketOps ops;
    ops.block_recv = false;

    SECTION("Disconnect after a successful connect")
    {
        IRCSocket socket(ops);
        REQUIRE(socket.Init());
        REQUIRE(socket.Connect("host", 6667));
        socket.Disconnect();
        socket.Disconnect();
        REQUIRE(ops.close_count == 1);
        REQUIRE_FALSE(socket.Connected());
    }

    SECTION("destructor of a connected socket")
    {
        {
            IRCSocket socket(ops);
            REQUIRE(socket.Init());
            REQUIRE(socket.Connect("host", 6667));
            REQUIRE(socket.Connected());
        }
        REQUIRE(ops.close_count == 1);
    }

    SECTION("failed connect still closes the created handle")
    {
        ops.connect_result = -1;
        IRCSocket socket(ops);
        REQUIRE(socket.Init());
        REQUIRE_FALSE(socket.Connect("host", 6667));
        REQUIRE(ops.create_count == 1);
        REQUIRE(ops.close_count == 1);
        REQUIRE_FALSE(socket.Connected());
    }

    SECTION("Init without Connect does not close")
    {
        {
            IRCSocket socket(ops);
            REQUIRE(socket.Init());
        }
        REQUIRE(ops.close_count == 0);
        REQUIRE(ops.create_count == 0);
    }
}

TEST_CASE("Disconnect wakes a blocked ReceiveData")
{
    FakeSocketOps ops;
    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    std::atomic<bool> finished{false};
    std::string result = "unset";
    std::jthread reader([&] {
        result = socket.ReceiveData();
        finished = true;
    });

    REQUIRE(ops.wait_until_recv_waiting(std::chrono::seconds{2}));
    socket.Disconnect();
    reader.join();

    REQUIRE(finished);
    REQUIRE(result.empty());
    REQUIRE_FALSE(socket.Connected());
    REQUIRE(ops.shutdown_count >= 1);
    REQUIRE(ops.close_count == 1);
}

TEST_CASE("Disconnect interrupts a blocked SendData")
{
    FakeSocketOps ops;
    ops.block_send = true;
    ops.block_recv = false;

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    std::atomic<bool> sender_done{false};
    std::atomic<bool> send_ok{true};
    std::atomic<bool> disconnect_done{false};
    std::thread sender([&] {
        send_ok = socket.SendData("payload");
        sender_done = true;
    });
    std::thread stopper;

    struct Cleanup
    {
        FakeSocketOps& ops;
        std::thread& sender;
        std::thread& stopper;
        ~Cleanup()
        {
            ops.unblock_send();
            if (sender.joinable())
                sender.join();
            if (stopper.joinable())
                stopper.join();
        }
    } cleanup{ops, sender, stopper};

    REQUIRE(ops.wait_until_send_waiting(std::chrono::seconds{2}));

    stopper = std::thread([&] {
        socket.Disconnect();
        disconnect_done = true;
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!disconnect_done.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{5});

    REQUIRE(disconnect_done.load());
    REQUIRE(sender_done.load());
    REQUIRE_FALSE(send_ok);
    REQUIRE_FALSE(socket.Connected());
    REQUIRE(ops.shutdown_count >= 1);
}

TEST_CASE("closed handles are not used after descriptor reuse")
{
    FakeSocketOps ops;
    ops.reuse_handle = 42;
    ops.block_recv = false;

    IRCSocket first(ops);
    REQUIRE(first.Init());
    REQUIRE(first.Connect("host", 6667));
    first.Disconnect();
    REQUIRE(ops.close_count == 1);

    const int sends_after_close = ops.send_count;
    REQUIRE_FALSE(first.SendData("stale"));
    REQUIRE(ops.send_count == sends_after_close);
    REQUIRE(first.ReceiveData().empty());
    REQUIRE(ops.recv_count == 0);

    IRCSocket second(ops);
    REQUIRE(second.Init());
    REQUIRE(second.Connect("host", 6667));
    REQUIRE(ops.create_count == 2);

    REQUIRE_FALSE(first.SendData("stale"));
    REQUIRE(ops.send_count == sends_after_close);
    REQUIRE(second.SendData("live"));
    REQUIRE(ops.sent == "live");
    REQUIRE(ops.ops_after_close == 0);
}

TEST_CASE("blocked recv finishes before close so a reused handle is not stolen")
{
    FakeSocketOps ops;
    ops.reuse_handle = 7;

    IRCSocket first(ops);
    REQUIRE(first.Init());
    REQUIRE(first.Connect("host", 6667));

    std::string result = "unset";
    std::jthread reader([&] { result = first.ReceiveData(); });
    REQUIRE(ops.wait_until_recv_waiting(std::chrono::seconds{2}));

    first.Disconnect();
    reader.join();

    REQUIRE(result.empty());
    REQUIRE(ops.close_count == 1);
    REQUIRE(ops.ops_after_close == 0);

    IRCSocket second(ops);
    REQUIRE(second.Init());
    REQUIRE(second.Connect("host", 6667));
    REQUIRE(second.SendData("ok"));
    REQUIRE(ops.sent == "ok");
    REQUIRE_FALSE(first.Connected());
    REQUIRE_FALSE(first.SendData("nope"));
}

TEST_CASE("Connect creates a fresh handle on each attempt")
{
    FakeSocketOps ops;
    ops.block_recv = false;

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));
    socket.Disconnect();
    REQUIRE(socket.Connect("host", 6667));
    REQUIRE(ops.create_count == 2);
    REQUIRE(ops.close_count == 1);
    socket.Disconnect();
    REQUIRE(ops.close_count == 2);
}

TEST_CASE("IRCSocket connects to LoopbackServer over IPv4 and IPv6")
{
    LoopbackServer server;
    REQUIRE(server.ipv4_port() > 0);
    REQUIRE(server.ipv6_port() > 0);

    const std::vector<std::uint8_t> payload{'p', 'i', 'n', 'g', 0x00, 'x'};

    SECTION("IPv4 127.0.0.1")
    {
        IRCSocket socket;
        REQUIRE(socket.Init());
        REQUIRE(socket.Connect("127.0.0.1", server.ipv4_port()));
        REQUIRE(server.wait_for_connection(std::chrono::seconds{2}));
        REQUIRE(socket.Connected());

        REQUIRE(socket.SendData("hello"));
        REQUIRE(server.wait_until_received(5, std::chrono::seconds{2}));
        const std::vector<std::uint8_t> hello{'h', 'e', 'l', 'l', 'o'};
        REQUIRE(server.received() == hello);

        server.send(std::span<const std::uint8_t>(payload));
        const std::string got = socket.ReceiveData();
        REQUIRE(got.size() == payload.size());
        REQUIRE(got[4] == '\0');
        REQUIRE(std::vector<std::uint8_t>(got.begin(), got.end()) == payload);

        socket.Disconnect();
        REQUIRE_FALSE(socket.Connected());
    }

    SECTION("IPv6 ::1")
    {
        IRCSocket socket;
        REQUIRE(socket.Init());
        REQUIRE(socket.Connect("::1", server.ipv6_port()));
        REQUIRE(server.wait_for_connection(std::chrono::seconds{2}));
        REQUIRE(socket.Connected());

        REQUIRE(socket.SendData("ipv6"));
        REQUIRE(server.wait_until_received(4, std::chrono::seconds{2}));
        const std::vector<std::uint8_t> ipv6{'i', 'p', 'v', '6'};
        REQUIRE(server.received() == ipv6);

        socket.Disconnect();
        REQUIRE_FALSE(socket.Connected());
    }
}
