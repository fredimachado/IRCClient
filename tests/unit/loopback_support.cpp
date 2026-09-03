#include "SocketOps.h"
#include "../support/LoopbackServer.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using RawSocket = SOCKET;
constexpr RawSocket kInvalidRaw = INVALID_SOCKET;

void close_raw(RawSocket s)
{
    if (s != kInvalidRaw)
        closesocket(s);
}
#else
using RawSocket = int;
constexpr RawSocket kInvalidRaw = -1;

void close_raw(RawSocket s)
{
    if (s != kInvalidRaw)
        ::close(s);
}
#endif

struct WinsockRef
{
#ifdef _WIN32
    WinsockRef()
    {
        WSADATA data{};
        REQUIRE(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    }

    ~WinsockRef()
    {
        WSACleanup();
    }
#else
    WinsockRef() = default;
#endif

    WinsockRef(const WinsockRef&) = delete;
    WinsockRef& operator=(const WinsockRef&) = delete;
};

class RawClient
{
public:
    RawClient(int family, int port)
    {
        socket_ = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(socket_ != kInvalidRaw);

#ifdef _WIN32
        DWORD timeout_ms = 2000;
        setsockopt(
            socket_,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeout_ms),
            sizeof(timeout_ms));
#else
        timeval timeout{};
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

        if (family == AF_INET)
        {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<std::uint16_t>(port));
            REQUIRE(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) == 1);
            REQUIRE(::connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        }
        else
        {
            sockaddr_in6 addr{};
            addr.sin6_family = AF_INET6;
            addr.sin6_port = htons(static_cast<std::uint16_t>(port));
            REQUIRE(inet_pton(AF_INET6, "::1", &addr.sin6_addr) == 1);
            REQUIRE(::connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        }
    }

    ~RawClient()
    {
        close_raw(socket_);
    }

    RawClient(const RawClient&) = delete;
    RawClient& operator=(const RawClient&) = delete;

    void send_all(const std::vector<std::uint8_t>& bytes)
    {
        std::size_t sent = 0;
        while (sent < bytes.size())
        {
            int n = ::send(
                socket_,
                reinterpret_cast<const char*>(bytes.data() + sent),
                static_cast<int>(bytes.size() - sent),
                0);
            REQUIRE(n > 0);
            sent += static_cast<std::size_t>(n);
        }
    }

    std::vector<std::uint8_t> recv_exact(std::size_t count)
    {
        std::vector<std::uint8_t> out(count);
        std::size_t got = 0;
        while (got < count)
        {
            int n = ::recv(
                socket_,
                reinterpret_cast<char*>(out.data() + got),
                static_cast<int>(count - got),
                0);
            REQUIRE(n > 0);
            got += static_cast<std::size_t>(n);
        }
        return out;
    }

private:
    RawSocket socket_ = kInvalidRaw;
};

void echo_roundtrip(LoopbackServer& server, int family, int port)
{
    const std::vector<std::uint8_t> payload{
        'e', 'c', 'h', 'o', 0x00, 0x01, '\r', '\n', 'x'};

    RawClient client(family, port);
    REQUIRE(server.wait_for_connection(std::chrono::seconds{2}));
    REQUIRE(server.connection_count() >= 1);

    client.send_all(payload);
    REQUIRE(server.wait_until_received(payload.size(), std::chrono::seconds{2}));
    REQUIRE(server.received() == payload);

    server.send(std::span<const std::uint8_t>(payload));
    REQUIRE(client.recv_exact(payload.size()) == payload);
}

} // namespace

static_assert(std::is_abstract_v<SocketOps>);
static_assert(SocketOps::invalid_handle == static_cast<SocketOps::handle_type>(-1));

TEST_CASE("loopback server binds, accepts, and echoes")
{
    [[maybe_unused]] WinsockRef winsock;
    LoopbackServer server;

    REQUIRE(server.ipv4_port() > 0);
    REQUIRE(server.ipv6_port() > 0);
    REQUIRE(server.ipv4_port() != server.ipv6_port());

    SECTION("IPv4 127.0.0.1")
    {
        echo_roundtrip(server, AF_INET, server.ipv4_port());
    }

    SECTION("IPv6 ::1")
    {
        echo_roundtrip(server, AF_INET6, server.ipv6_port());
    }
}
