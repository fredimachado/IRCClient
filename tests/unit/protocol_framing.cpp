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

void CaptureHook(IRCMessage message, IRCClient*)
{
    g_seen.push_back(std::move(message));
}

void ResetCapture()
{
    g_seen.clear();
}
}

TEST_CASE("fragmented frames are retained until CR LF arrives")
{
    ResetCapture();
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);
    client.HookIRCCommand("PING", &CaptureHook);

    ops.push_recv("PIN");
    client.ReceiveData();
    REQUIRE(g_seen.empty());
    REQUIRE(ops.sent.empty());

    ops.push_recv("G :token");
    client.ReceiveData();
    REQUIRE(g_seen.empty());
    REQUIRE(ops.sent.empty());

    ops.push_recv("\r\n");
    client.ReceiveData();
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].command == "PING");
    REQUIRE(g_seen[0].parameters.size() == 1);
    REQUIRE(g_seen[0].parameters[0] == "token");
    REQUIRE(ops.sent == "PONG :token\r\n");
}

TEST_CASE("coalesced frames are split on CR LF")
{
    ResetCapture();
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);
    client.HookIRCCommand("PING", &CaptureHook);

    ops.push_recv("PING :one\r\nPING :two\r\n");
    client.ReceiveData();

    REQUIRE(g_seen.size() == 2);
    REQUIRE(g_seen[0].parameters[0] == "one");
    REQUIRE(g_seen[1].parameters[0] == "two");
    REQUIRE(ops.sent == "PONG :one\r\nPONG :two\r\n");
}

TEST_CASE("PING answers with exact CR LF")
{
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);

    ops.push_recv("PING :irc.example.net\r\n");
    client.ReceiveData();
    REQUIRE(ops.sent == "PONG :irc.example.net\r\n");
}

TEST_CASE("embedded NUL in a frame is rejected and later frames recover")
{
    ResetCapture();
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);
    client.HookIRCCommand("PRIVMSG", &CaptureHook);
    client.HookIRCCommand("PING", &CaptureHook);

    std::string wire = "PRIVMSG #c :hel";
    wire.push_back('\0');
    wire += "lo\r\nPING :ok\r\n";
    ops.push_recv(std::move(wire));
    client.ReceiveData();

    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].command == "PING");
    REQUIRE(g_seen[0].parameters[0] == "ok");
    REQUIRE(ops.sent == "PONG :ok\r\n");
}

TEST_CASE("overlong complete frame is discarded and the next frame is parsed")
{
    ResetCapture();
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);
    client.HookIRCCommand("PRIVMSG", &CaptureHook);
    client.HookIRCCommand("PING", &CaptureHook);

    std::string overlong = ":n!u@h PRIVMSG #c :";
    overlong.append(IRCClient::kMaxIrcFrameBytes, 'A');
    overlong += "\r\nPING :recovered\r\n";
    ops.push_recv(std::move(overlong));
    client.ReceiveData();

    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].command == "PING");
    REQUIRE(g_seen[0].parameters[0] == "recovered");
    REQUIRE(ops.sent == "PONG :recovered\r\n");
}

TEST_CASE("overlong frame without delimiter enters discard then recovers on CR LF")
{
    ResetCapture();
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);
    client.HookIRCCommand("PING", &CaptureHook);

    ops.push_recv(std::string(IRCClient::kMaxIrcFrameBytes, 'X'));
    client.ReceiveData();
    REQUIRE(g_seen.empty());
    REQUIRE(ops.sent.empty());

    ops.push_recv("YYYY\r\nPING :after\r\n");
    client.ReceiveData();
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].parameters[0] == "after");
    REQUIRE(ops.sent == "PONG :after\r\n");
}

TEST_CASE("a 512-byte frame including CR LF is accepted and 513 is not")
{
    ResetCapture();
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);
    client.HookIRCCommand("PING", &CaptureHook);

    // PING : + 504 bytes + CRLF = 512
    std::string exact = "PING :";
    exact.append(504, 'z');
    exact += "\r\n";
    REQUIRE(exact.size() == IRCClient::kMaxIrcFrameBytes);

    ops.push_recv(exact);
    client.ReceiveData();
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].parameters[0].size() == 504);

    g_seen.clear();
    ops.sent.clear();

    std::string too_long = "PING :";
    too_long.append(505, 'z');
    too_long += "\r\nPING :short\r\n";
    REQUIRE(too_long.find("\r\n") + 2 == IRCClient::kMaxIrcFrameBytes + 1);

    ops.push_recv(std::move(too_long));
    client.ReceiveData();
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].parameters[0] == "short");
}
