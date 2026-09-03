#include "IRCClient.h"
#include "SocketOps.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace
{
class FakeSocketOps final : public SocketOps
{
public:
    handle_type create() override
    {
        live_ = next_++;
        return live_;
    }

    int connect(handle_type, std::string_view, int) override
    {
        return 0;
    }

    int send(handle_type, const char* buffer, std::size_t length) override
    {
        if (buffer == nullptr)
            return -1;
        sent.append(buffer, length);
        ++send_count;
        return static_cast<int>(length);
    }

    int recv(handle_type, char* buffer, std::size_t length) override
    {
        if (recv_chunks.empty())
            return 0;
        std::string chunk = std::move(recv_chunks.front());
        recv_chunks.pop_front();
        std::size_t const n = (std::min)(length, chunk.size());
        std::memcpy(buffer, chunk.data(), n);
        return static_cast<int>(n);
    }

    int shutdown(handle_type) override
    {
        return 0;
    }

    int close(handle_type) override
    {
        live_ = invalid_handle;
        return 0;
    }

    [[nodiscard]] bool last_error_is_retryable() const override
    {
        return false;
    }

    void push_recv(std::string data)
    {
        recv_chunks.push_back(std::move(data));
    }

    std::string sent;
    int send_count = 0;
    std::deque<std::string> recv_chunks;

private:
    handle_type next_ = 1;
    handle_type live_ = invalid_handle;
};

void PrimeConnected(IRCClient& client)
{
    char host[] = "127.0.0.1";
    REQUIRE(client.InitSocket());
    REQUIRE(client.Connect(host, 6667));
    REQUIRE(client.Connected());
}

std::vector<IRCMessage> g_seen;
std::string const* g_sent_view = nullptr;
bool g_pong_before_hook = false;
bool g_connected_in_error_hook = true;

void CaptureHook(IRCMessage message, IRCClient*)
{
    g_seen.push_back(std::move(message));
}

void PingAfterBuiltinHook(IRCMessage message, IRCClient*)
{
    g_seen.push_back(std::move(message));
    g_pong_before_hook = g_sent_view != nullptr && *g_sent_view == "PONG :token\r\n";
}

void ErrorAfterBuiltinHook(IRCMessage message, IRCClient* client)
{
    g_seen.push_back(std::move(message));
    g_connected_in_error_hook = client != nullptr && client->Connected();
}

void ResetCapture()
{
    g_seen.clear();
    g_sent_view = nullptr;
    g_pong_before_hook = false;
    g_connected_in_error_hook = true;
}
}

TEST_CASE("hook command names match regardless of register or dispatch case")
{
    ResetCapture();
    IRCClient client;
    client.HookIRCCommand("privmsg", &CaptureHook);

    client.Parse(":n!u@h PRIVMSG #c :hello");
    client.Parse(":n!u@h privmsg #c :again");

    REQUIRE(g_seen.size() == 2);
    REQUIRE(g_seen[0].command == "PRIVMSG");
    REQUIRE(g_seen[1].command == "PRIVMSG");
    REQUIRE(g_seen[0].parameters.back() == "hello");
    REQUIRE(g_seen[1].parameters.back() == "again");
}

TEST_CASE("PING runs built-in PONG first then the matching hook")
{
    ResetCapture();
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);
    g_sent_view = &ops.sent;
    client.HookIRCCommand("ping", &PingAfterBuiltinHook);

    ops.push_recv("PING :token\r\n");
    client.ReceiveData();

    REQUIRE(ops.sent == "PONG :token\r\n");
    REQUIRE(g_pong_before_hook);
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].command == "PING");
    REQUIRE(g_seen[0].parameters[0] == "token");
}

TEST_CASE("ERROR disconnects first then invokes the matching hook")
{
    ResetCapture();
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);
    client.HookIRCCommand("error", &ErrorAfterBuiltinHook);

    ops.push_recv("ERROR :closing link\r\n");
    client.ReceiveData();

    REQUIRE_FALSE(client.Connected());
    REQUIRE_FALSE(g_connected_in_error_hook);
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].command == "ERROR");
    REQUIRE(g_seen[0].parameters.size() == 1);
    REQUIRE(g_seen[0].parameters[0] == "closing link");
}

TEST_CASE("PING without a hook still answers and ERROR without a hook still disconnects")
{
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);

    ops.push_recv("PING :solo\r\n");
    client.ReceiveData();
    REQUIRE(ops.sent == "PONG :solo\r\n");
    REQUIRE(client.Connected());

    ops.push_recv("ERROR :bye\r\n");
    client.ReceiveData();
    REQUIRE_FALSE(client.Connected());
}
