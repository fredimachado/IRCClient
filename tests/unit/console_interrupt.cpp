#include "ConsoleInput.h"
#include "ConsoleInterrupt.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace
{
class BlockingStdin
{
public:
    BlockingStdin()
    {
#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        if (!CreatePipe(&read_, &write_, &sa, 0))
            return;
        previous_ = GetStdHandle(STD_INPUT_HANDLE);
        ok_ = SetStdHandle(STD_INPUT_HANDLE, read_) != FALSE;
#else
        int fds[2] = {-1, -1};
        if (pipe(fds) != 0)
            return;
        read_ = fds[0];
        write_ = fds[1];
        saved_ = dup(STDIN_FILENO);
        if (saved_ < 0)
            return;
        ok_ = dup2(read_, STDIN_FILENO) >= 0;
#endif
    }

    ~BlockingStdin()
    {
#ifdef _WIN32
        if (previous_ != nullptr && previous_ != INVALID_HANDLE_VALUE)
            SetStdHandle(STD_INPUT_HANDLE, previous_);
        if (write_ != nullptr && write_ != INVALID_HANDLE_VALUE)
            CloseHandle(write_);
        if (read_ != nullptr && read_ != INVALID_HANDLE_VALUE)
            CloseHandle(read_);
#else
        if (saved_ >= 0)
        {
            dup2(saved_, STDIN_FILENO);
            close(saved_);
        }
        if (read_ >= 0)
            close(read_);
        if (write_ >= 0)
            close(write_);
#endif
    }

    BlockingStdin(const BlockingStdin&) = delete;
    BlockingStdin& operator=(const BlockingStdin&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }

private:
    bool ok_ = false;
#ifdef _WIN32
    HANDLE read_ = nullptr;
    HANDLE write_ = nullptr;
    HANDLE previous_ = INVALID_HANDLE_VALUE;
#else
    int read_ = -1;
    int write_ = -1;
    int saved_ = -1;
#endif
};

bool wait_flag(std::atomic<bool> const& flag, std::chrono::milliseconds timeout)
{
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return true;
}
}

TEST_CASE("ConsoleInterrupt request invokes the callback once")
{
    std::atomic<int> hits{0};
    ConsoleInterrupt interrupt([&] { hits.fetch_add(1, std::memory_order_relaxed); });
    REQUIRE(interrupt.valid());
    REQUIRE_FALSE(interrupt.interrupted());

    interrupt.request();
    REQUIRE(interrupt.interrupted());
    REQUIRE(hits.load(std::memory_order_relaxed) == 1);

    interrupt.request();
    REQUIRE(hits.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("ConsoleInterrupt request is idempotent across threads")
{
    std::atomic<int> hits{0};
    ConsoleInterrupt interrupt([&] { hits.fetch_add(1, std::memory_order_relaxed); });
    REQUIRE(interrupt.valid());

    std::thread a([&] { interrupt.request(); });
    std::thread b([&] { interrupt.request(); });
    a.join();
    b.join();

    REQUIRE(interrupt.interrupted());
    REQUIRE(hits.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("ConsoleInterrupt allows only one active instance")
{
    ConsoleInterrupt first([] {});
    REQUIRE(first.valid());

    ConsoleInterrupt second([] {});
    REQUIRE_FALSE(second.valid());
}

TEST_CASE("ConsoleInterrupt can be constructed and destroyed repeatedly")
{
    for (int i = 0; i < 3; ++i)
    {
        std::atomic<int> hits{0};
        ConsoleInterrupt interrupt([&] { hits.fetch_add(1, std::memory_order_relaxed); });
        REQUIRE(interrupt.valid());
        interrupt.request();
        REQUIRE(hits.load(std::memory_order_relaxed) == 1);
    }
}

#ifndef _WIN32
TEST_CASE("ConsoleInterrupt delivers SIGINT on a sigwait thread")
{
    std::atomic<int> hits{0};
    ConsoleInterrupt interrupt([&] { hits.fetch_add(1, std::memory_order_relaxed); });
    REQUIRE(interrupt.valid());

    REQUIRE(kill(getpid(), SIGINT) == 0);

    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (hits.load(std::memory_order_relaxed) == 0
        && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    REQUIRE(hits.load(std::memory_order_relaxed) == 1);
    REQUIRE(interrupt.interrupted());
}
#endif

TEST_CASE("ConsoleInput request_stop is idempotent")
{
    ConsoleInput console;
    REQUIRE(console.valid());
    REQUIRE_FALSE(console.stop_requested());

    console.request_stop();
    console.request_stop();
    REQUIRE(console.stop_requested());

    ConsoleInput::ReadResult const result = console.read_line();
    REQUIRE(result.status == ConsoleInput::Status::Stopped);
}

TEST_CASE("ConsoleInput request_stop wakes a blocked read")
{
    BlockingStdin stdin_pipe;
    REQUIRE(stdin_pipe.ok());

    ConsoleInput console;
    REQUIRE(console.valid());

    std::atomic<bool> finished{false};
    ConsoleInput::Status status = ConsoleInput::Status::Error;

    std::thread reader([&] {
        ConsoleInput::ReadResult const result = console.read_line();
        status = result.status;
        finished.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    bool const blocked = !finished.load(std::memory_order_acquire);

    console.request_stop();
    console.request_stop();

    bool const stopped_in_time = wait_flag(finished, std::chrono::seconds{2});
    if (reader.joinable())
        reader.join();

    REQUIRE(blocked);
    REQUIRE(stopped_in_time);
    REQUIRE(status == ConsoleInput::Status::Stopped);
    REQUIRE(console.stop_requested());
}
