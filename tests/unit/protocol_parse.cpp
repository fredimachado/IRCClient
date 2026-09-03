#include "IRCClient.h"
#include "IRCHandler.h"
#include "SocketOps.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <deque>
#include <iostream>
#include <sstream>
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

class CoutCapture
{
public:
    CoutCapture() : old_(std::cout.rdbuf(buffer_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old_); }

private:
    std::ostringstream buffer_;
    std::streambuf* old_;
};
}

TEST_CASE("default IRCClient constructor remains available")
{
    IRCClient client;
    REQUIRE_FALSE(client.Connected());
}

TEST_CASE("trailing parameters keep empty and non-empty text")
{
    ResetCapture();
    IRCClient client;
    client.HookIRCCommand("PRIVMSG", &CaptureHook);

    client.Parse(":n!u@h PRIVMSG #c :hello world");
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].parameters.size() == 2);
    REQUIRE(g_seen[0].parameters[0] == "#c");
    REQUIRE(g_seen[0].parameters[1] == "hello world");

    g_seen.clear();
    client.Parse(":n!u@h PRIVMSG #c :");
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].parameters.size() == 2);
    REQUIRE(g_seen[0].parameters[0] == "#c");
    REQUIRE(g_seen[0].parameters[1] == "");
}

TEST_CASE("every middle parameter is preserved")
{
    ResetCapture();
    IRCClient client;
    client.HookIRCCommand("PRIVMSG", &CaptureHook);

    client.Parse("PRIVMSG #channel hello");
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].parameters.size() == 2);
    REQUIRE(g_seen[0].parameters[0] == "#channel");
    REQUIRE(g_seen[0].parameters[1] == "hello");
}

TEST_CASE("at most 15 parameters are parsed")
{
    ResetCapture();
    IRCClient client;
    client.HookIRCCommand("TEST", &CaptureHook);

    client.Parse("TEST 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17");
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].parameters.size() == 15);
    REQUIRE(g_seen[0].parameters[0] == "1");
    REQUIRE(g_seen[0].parameters[13] == "14");
    REQUIRE(g_seen[0].parameters[14] == "15 16 17");
}

TEST_CASE("malformed prefixes and commands do not crash and are skipped")
{
    ResetCapture();
    IRCClient client;
    client.HookIRCCommand("PRIVMSG", &CaptureHook);
    client.HookIRCCommand("PING", &CaptureHook);

    REQUIRE_NOTHROW(client.Parse(":nick! PRIVMSG #c :hi"));
    REQUIRE_NOTHROW(client.Parse(":nick@ PRIVMSG #c :hi"));
    REQUIRE_NOTHROW(client.Parse(":@host PRIVMSG #c :hi"));
    REQUIRE_NOTHROW(client.Parse(":nospacePRIVMSG"));
    REQUIRE_NOTHROW(client.Parse(": "));
    REQUIRE_NOTHROW(client.Parse("!!!"));
    REQUIRE_NOTHROW(client.Parse("22"));
    REQUIRE_NOTHROW(client.Parse("2222"));
    REQUIRE_NOTHROW(client.Parse("@tag=x :n!u@h PRIVMSG #c :hi"));
    REQUIRE(g_seen.empty());

    client.Parse(":n!u@h PRIVMSG #c :ok");
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].prefix.nick == "n");
    REQUIRE(g_seen[0].prefix.user == "u");
    REQUIRE(g_seen[0].prefix.host == "h");
    REQUIRE(SenderName(g_seen[0].prefix) == "n");
}

TEST_CASE("nick-only prefixes keep the raw prefix and leave nick empty")
{
    ResetCapture();
    IRCClient client;
    client.HookIRCCommand("PRIVMSG", &CaptureHook);
    CoutCapture silence;

    client.Parse(":alice PRIVMSG #room :hello");
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].prefix.prefix == "alice");
    REQUIRE(g_seen[0].prefix.nick.empty());
    REQUIRE(g_seen[0].prefix.user.empty());
    REQUIRE(g_seen[0].prefix.host.empty());
    REQUIRE(SenderName(g_seen[0].prefix) == "alice");
}

TEST_CASE("server-name prefixes keep the raw prefix and leave nick empty")
{
    ResetCapture();
    IRCClient client;
    client.HookIRCCommand("PRIVMSG", &CaptureHook);
    CoutCapture silence;

    client.Parse(":irc.example.net PRIVMSG #room :hello");
    REQUIRE(g_seen.size() == 1);
    REQUIRE(g_seen[0].prefix.prefix == "irc.example.net");
    REQUIRE(g_seen[0].prefix.nick.empty());
    REQUIRE(g_seen[0].prefix.user.empty());
    REQUIRE(g_seen[0].prefix.host.empty());
    REQUIRE(SenderName(g_seen[0].prefix).empty());
}

TEST_CASE("malformed CTCP and short handler parameter lists do not crash")
{
    IRCClient client;

    REQUIRE_NOTHROW(client.Parse("PRIVMSG"));
    REQUIRE_NOTHROW(client.Parse("PRIVMSG :"));
    REQUIRE_NOTHROW(client.Parse("PRIVMSG nick :\001"));
    REQUIRE_NOTHROW(client.Parse("PRIVMSG nick :\001VERSION"));
    REQUIRE_NOTHROW(client.Parse("NOTICE"));
    REQUIRE_NOTHROW(client.Parse("NOTICE :\001"));
    REQUIRE_NOTHROW(client.Parse("JOIN"));
    REQUIRE_NOTHROW(client.Parse("PART"));
    REQUIRE_NOTHROW(client.Parse("NICK"));
    REQUIRE_NOTHROW(client.Parse("QUIT"));
    REQUIRE_NOTHROW(client.Parse("353"));
    REQUIRE_NOTHROW(client.Parse("353 a b"));
    REQUIRE_NOTHROW(client.Parse("433"));
    REQUIRE_NOTHROW(client.Parse("433 nick"));
    REQUIRE_NOTHROW(client.Parse("001"));
    REQUIRE_NOTHROW(client.HandlePrivMsg(IRCMessage{}));
    REQUIRE_NOTHROW(client.HandleNotice(IRCMessage{}));
    REQUIRE_NOTHROW(client.HandleChannelJoinPart(IRCMessage{}));
    REQUIRE_NOTHROW(client.HandleUserNickChange(IRCMessage{}));
    REQUIRE_NOTHROW(client.HandleUserQuit(IRCMessage{}));
    REQUIRE_NOTHROW(client.HandleChannelNamesList(IRCMessage{}));
    REQUIRE_NOTHROW(client.HandleNicknameInUse(IRCMessage{}));
    REQUIRE_NOTHROW(client.HandleServerMessage(IRCMessage{}));
    REQUIRE_NOTHROW(client.HandleCTCP(IRCMessage{}));
}

TEST_CASE("registration emits NICK and USER with CR LF and no HELLO")
{
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);

    REQUIRE(client.Login("alice", "aliceuser"));
    REQUIRE(ops.send_count == 1);
    REQUIRE(ops.sent == "NICK alice\r\nUSER aliceuser 8 * :Cpp IRC Client\r\n");
    REQUIRE(ops.sent.find("HELLO") == std::string::npos);
}

TEST_CASE("registration with PASS is one atomic write")
{
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);

    REQUIRE(client.Login("alice", "aliceuser", "s3cret"));
    REQUIRE(ops.send_count == 1);
    REQUIRE(ops.sent ==
            "PASS s3cret\r\nNICK alice\r\nUSER aliceuser 8 * :Cpp IRC Client\r\n");
    REQUIRE(ops.sent.find("HELLO") == std::string::npos);
}

TEST_CASE("invalid registration fields send nothing")
{
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);

    REQUIRE_FALSE(client.Login("", "user"));
    REQUIRE_FALSE(client.Login("nick", ""));
    REQUIRE_FALSE(client.Login("ni ck", "user"));
    REQUIRE_FALSE(client.Login("nick", "us er"));
    REQUIRE_FALSE(client.Login("nick", "user", "pass word"));
    REQUIRE_FALSE(client.Login("nick\nnick", "user"));
    std::string password_with_nul = "p";
    password_with_nul.push_back('\0');
    password_with_nul += "ass";
    REQUIRE_FALSE(client.Login("nick", "user", password_with_nul));
    REQUIRE(ops.sent.empty());
    REQUIRE(ops.send_count == 0);
}

TEST_CASE("outbound CR LF and NUL injection is rejected")
{
    FakeSocketOps ops;
    IRCClient client(ops);
    PrimeConnected(client);

    REQUIRE_FALSE(client.SendIRC("PRIVMSG #c :hi\r\nQUIT"));
    REQUIRE_FALSE(client.SendIRC("PRIVMSG #c :hi\nQUIT"));
    REQUIRE_FALSE(client.SendIRC("PRIVMSG #c :hi\rQUIT"));
    std::string with_nul = "PRIVMSG #c :hi";
    with_nul.push_back('\0');
    with_nul += "there";
    REQUIRE_FALSE(client.SendIRC(with_nul));
    REQUIRE(ops.sent.empty());
    REQUIRE(ops.send_count == 0);

    REQUIRE(client.SendIRC("JOIN #chan"));
    REQUIRE(ops.sent == "JOIN #chan\r\n");
}
