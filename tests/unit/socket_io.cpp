#include "IRCSocket.h"
#include "SocketOps.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace
{
class FakeSocketOps final : public SocketOps
{
public:
    enum class RecvKind
    {
        Data,
        Eof,
        HardError,
        Retryable
    };

    struct RecvEvent
    {
        RecvKind kind = RecvKind::Eof;
        std::string data;
    };

    handle_type create() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++create_count;
        live_handle_ = next_handle_++;
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
        if (buffer == nullptr)
            return -1;

        std::unique_lock<std::mutex> lock(mutex_);
        ++send_count;
        send_cv_.notify_all();
        if (socket != live_handle_)
            ++stale_ops;

        if (send_abort_)
        {
            retryable_ = false;
            return -1;
        }

        if (send_hard_error)
        {
            retryable_ = false;
            return -1;
        }

        if (send_always_retryable)
        {
            retryable_ = true;
            send_cv_.wait_for(lock, std::chrono::milliseconds{20}, [this] {
                return send_abort_ || !send_always_retryable;
            });
            if (send_abort_)
            {
                retryable_ = false;
                return -1;
            }
            return -1;
        }

        if (send_hard_error_after > 0 && send_count > send_hard_error_after)
        {
            retryable_ = false;
            return -1;
        }

        if (send_retryable_remaining > 0)
        {
            --send_retryable_remaining;
            retryable_ = true;
            return -1;
        }

        if (length == 0)
            return 0;

        const std::size_t n = (std::min)(length, send_limit);
        sent.append(buffer, n);

        const auto delay = send_delay;
        lock.unlock();
        if (delay.count() > 0)
            std::this_thread::sleep_for(delay);
        return static_cast<int>(n);
    }

    int recv(handle_type socket, char* buffer, std::size_t length) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ++recv_count;
        if (socket != live_handle_)
            ++stale_ops;

        if (recv_script.empty())
        {
            retryable_ = false;
            return 0;
        }

        RecvEvent event = recv_script.front();
        recv_script.pop_front();

        switch (event.kind)
        {
        case RecvKind::Eof:
            return 0;
        case RecvKind::HardError:
            retryable_ = false;
            return -1;
        case RecvKind::Retryable:
            retryable_ = true;
            return -1;
        case RecvKind::Data:
        {
            const std::size_t n = (std::min)(length, event.data.size());
            std::memcpy(buffer, event.data.data(), n);
            return static_cast<int>(n);
        }
        }
        return -1;
    }

    int shutdown(handle_type) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++shutdown_count;
        return 0;
    }

    int close(handle_type socket) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++close_count;
        if (socket != live_handle_)
            ++stale_ops;
        live_handle_ = invalid_handle;
        return 0;
    }

    [[nodiscard]] bool last_error_is_retryable() const override
    {
        return retryable_;
    }

    void push_recv(RecvKind kind, std::string data = {})
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recv_script.push_back(RecvEvent{kind, std::move(data)});
    }

    bool wait_until_send_count(int min_count, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return send_cv_.wait_for(lock, timeout, [&] { return send_count >= min_count; });
    }

    void abort_sends()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        send_abort_ = true;
        send_always_retryable = false;
        send_cv_.notify_all();
    }

    int connect_result = 0;
    std::size_t send_limit = static_cast<std::size_t>(-1);
    int send_retryable_remaining = 0;
    int send_hard_error_after = 0;
    bool send_hard_error = false;
    bool send_always_retryable = false;
    std::chrono::milliseconds send_delay{0};

    std::string sent;
    int create_count = 0;
    int connect_count = 0;
    int send_count = 0;
    int recv_count = 0;
    int shutdown_count = 0;
    int close_count = 0;
    int stale_ops = 0;

    std::deque<RecvEvent> recv_script;

private:
    mutable std::mutex mutex_;
    std::condition_variable send_cv_;
    handle_type next_handle_ = 1;
    handle_type live_handle_ = invalid_handle;
    bool retryable_ = false;
    bool send_abort_ = false;
};

bool wait_flag(std::atomic<bool> const& flag, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return true;
}
} // namespace

TEST_CASE("SendData retries partial sends until every byte is written")
{
    FakeSocketOps ops;
    ops.send_limit = 3;

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    const char* message = "123456789";
    REQUIRE(socket.SendData(message));
    REQUIRE(ops.sent == message);
    REQUIRE(ops.send_count == 3);
    REQUIRE(socket.Connected());
}

TEST_CASE("SendData retries retryable errors then completes")
{
    FakeSocketOps ops;
    ops.send_limit = 2;
    ops.send_retryable_remaining = 2;

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));
    REQUIRE(socket.SendData("abcd"));
    REQUIRE(ops.sent == "abcd");
    REQUIRE(ops.send_count == 4);
}

TEST_CASE("SendData does not report success when the send cannot complete")
{
    FakeSocketOps ops;
    ops.send_limit = 2;
    ops.send_hard_error_after = 1;

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));
    REQUIRE_FALSE(socket.SendData("abcdef"));
    REQUIRE(ops.sent == "ab");
}

TEST_CASE("SendData fails while disconnected")
{
    FakeSocketOps ops;
    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE_FALSE(socket.SendData("hello"));
    REQUIRE(ops.send_count == 0);

    REQUIRE(socket.Connect("host", 6667));
    socket.Disconnect();
    REQUIRE_FALSE(socket.Connected());
    REQUIRE_FALSE(socket.SendData("hello"));
    REQUIRE(ops.send_count == 0);
}

TEST_CASE("concurrent SendData does not interleave message bytes")
{
    FakeSocketOps ops;
    ops.send_limit = 1;
    ops.send_delay = std::chrono::milliseconds(1);

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    const std::string a(32, 'A');
    const std::string b(32, 'B');
    std::atomic<bool> ok_a{false};
    std::atomic<bool> ok_b{false};

    std::jthread t1([&] { ok_a = socket.SendData(a.c_str()); });
    std::jthread t2([&] { ok_b = socket.SendData(b.c_str()); });
    t1.join();
    t2.join();

    REQUIRE(ok_a);
    REQUIRE(ok_b);
    REQUIRE(ops.sent.size() == 64);
    const std::string aa(32, 'A');
    const std::string bb(32, 'B');
    REQUIRE((ops.sent == aa + bb || ops.sent == bb + aa));
}

TEST_CASE("ReceiveData returns bytes from the recv count including embedded NUL")
{
    FakeSocketOps ops;
    std::string payload("ab");
    payload.push_back('\0');
    payload += "cd";
    ops.push_recv(FakeSocketOps::RecvKind::Data, payload);

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    const std::string got = socket.ReceiveData();
    REQUIRE(got.size() == 5);
    REQUIRE(got[2] == '\0');
    REQUIRE(got == payload);
    REQUIRE(socket.Connected());
}

TEST_CASE("ReceiveData treats EOF as empty string and disconnect")
{
    FakeSocketOps ops;
    ops.push_recv(FakeSocketOps::RecvKind::Eof);

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    REQUIRE(socket.ReceiveData().empty());
    REQUIRE_FALSE(socket.Connected());
    REQUIRE(ops.close_count == 1);
}

TEST_CASE("ReceiveData retries retryable errors then returns data")
{
    FakeSocketOps ops;
    ops.push_recv(FakeSocketOps::RecvKind::Retryable);
    ops.push_recv(FakeSocketOps::RecvKind::Retryable);
    ops.push_recv(FakeSocketOps::RecvKind::Data, "ok");

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    REQUIRE(socket.ReceiveData() == "ok");
    REQUIRE(ops.recv_count == 3);
    REQUIRE(socket.Connected());
}

TEST_CASE("ReceiveData disconnects on a hard recv error")
{
    FakeSocketOps ops;
    ops.push_recv(FakeSocketOps::RecvKind::HardError);

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    REQUIRE(socket.ReceiveData().empty());
    REQUIRE_FALSE(socket.Connected());
    REQUIRE(ops.recv_count == 1);
    REQUIRE(ops.close_count == 1);
}

TEST_CASE("ReceiveData fails while disconnected")
{
    FakeSocketOps ops;
    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.ReceiveData().empty());
    REQUIRE(ops.recv_count == 0);
}

TEST_CASE("SendData stops retrying retryable errors after Disconnect")
{
    FakeSocketOps ops;
    ops.send_always_retryable = true;

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    std::atomic<bool> sender_done{false};
    std::atomic<bool> send_ok{true};
    std::atomic<bool> disconnect_done{false};
    std::thread sender([&] {
        send_ok = socket.SendData("hello");
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
            ops.abort_sends();
            if (sender.joinable())
                sender.join();
            if (stopper.joinable())
                stopper.join();
        }
    } cleanup{ops, sender, stopper};

    REQUIRE(ops.wait_until_send_count(1, std::chrono::seconds{2}));

    stopper = std::thread([&] {
        socket.Disconnect();
        disconnect_done = true;
    });

    REQUIRE(wait_flag(disconnect_done, std::chrono::seconds{2}));
    REQUIRE(wait_flag(sender_done, std::chrono::seconds{2}));
    REQUIRE_FALSE(send_ok);
    REQUIRE_FALSE(socket.Connected());
}

TEST_CASE("SendData disconnects on a hard send error")
{
    FakeSocketOps ops;
    ops.send_hard_error = true;

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    REQUIRE_FALSE(socket.SendData("hello"));
    REQUIRE_FALSE(socket.Connected());
    REQUIRE(ops.close_count == 1);
}

TEST_CASE("SendData disconnects when send returns zero")
{
    FakeSocketOps ops;
    ops.send_limit = 0;

    IRCSocket socket(ops);
    REQUIRE(socket.Init());
    REQUIRE(socket.Connect("host", 6667));

    REQUIRE_FALSE(socket.SendData("hello"));
    REQUIRE_FALSE(socket.Connected());
    REQUIRE(ops.close_count == 1);
}
