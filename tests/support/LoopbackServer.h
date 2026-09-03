#ifndef IRCCLIENT_LOOPBACK_SERVER_H
#define IRCCLIENT_LOOPBACK_SERVER_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

// IPv4 (127.0.0.1) and IPv6 (::1) loopback TCP server for tests.
// Binds ephemeral ports, accepts connections, records exact received bytes,
// and can send scripted bytes. All waits are bounded.
class LoopbackServer
{
public:
    LoopbackServer();
    ~LoopbackServer();

    LoopbackServer(const LoopbackServer&) = delete;
    LoopbackServer& operator=(const LoopbackServer&) = delete;
    LoopbackServer(LoopbackServer&&) = delete;
    LoopbackServer& operator=(LoopbackServer&&) = delete;

    [[nodiscard]] int ipv4_port() const noexcept;
    [[nodiscard]] int ipv6_port() const noexcept;

    bool wait_for_connection(std::chrono::milliseconds timeout);
    bool wait_for_connections(std::size_t count, std::chrono::milliseconds timeout);
    [[nodiscard]] std::size_t connection_count() const;

    [[nodiscard]] std::vector<std::uint8_t> received(std::size_t index = 0) const;
    bool wait_until_received(
        std::size_t min_bytes,
        std::chrono::milliseconds timeout,
        std::size_t index = 0);

    void send(std::span<const std::uint8_t> bytes, std::size_t index = 0);
    void send(std::string_view bytes, std::size_t index = 0);

    // Stop reading from the connection so the peer's send buffers fill and
    // a blocking send can stall.
    void stall_reads(std::size_t index = 0);

    // Abortive close (SO_LINGER timeout 0) so the peer sees a reset on send.
    void reset_peer(std::size_t index = 0);

    [[nodiscard]] std::size_t unread(std::size_t index = 0) const;
    bool wait_until_unread(
        std::size_t min_bytes,
        std::chrono::milliseconds timeout,
        std::size_t index = 0);
    bool wait_until_unread_stable(
        std::size_t min_bytes,
        std::chrono::milliseconds stable_for,
        std::chrono::milliseconds timeout,
        std::size_t index = 0);

    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
