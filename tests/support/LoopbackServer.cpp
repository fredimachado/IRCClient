#include "LoopbackServer.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

int last_socket_error()
{
    return WSAGetLastError();
}

void native_close(NativeSocket s)
{
    closesocket(s);
}

bool is_retryable(int err)
{
    return err == WSAEINTR || err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;

int last_socket_error()
{
    return errno;
}

void native_close(NativeSocket s)
{
    ::close(s);
}

bool is_retryable(int err)
{
    return err == EINTR || err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS;
}
#endif

void set_nonblocking(NativeSocket s)
{
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags >= 0)
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

void set_cloexec(NativeSocket s)
{
#ifdef _WIN32
    SetHandleInformation(reinterpret_cast<HANDLE>(s), HANDLE_FLAG_INHERIT, 0);
#else
    fcntl(s, F_SETFD, FD_CLOEXEC);
#endif
}

int native_send(NativeSocket s, const char* data, int length)
{
#ifdef MSG_NOSIGNAL
    return ::send(s, data, static_cast<size_t>(length), MSG_NOSIGNAL);
#else
    return ::send(s, data, length, 0);
#endif
}

class UniqueSocket
{
public:
    UniqueSocket() = default;

    explicit UniqueSocket(NativeSocket s) noexcept : socket_(s) {}

    ~UniqueSocket()
    {
        reset();
    }

    UniqueSocket(UniqueSocket&& other) noexcept : socket_(other.socket_)
    {
        other.socket_ = kInvalidSocket;
    }

    UniqueSocket& operator=(UniqueSocket&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            socket_ = other.socket_;
            other.socket_ = kInvalidSocket;
        }
        return *this;
    }

    UniqueSocket(const UniqueSocket&) = delete;
    UniqueSocket& operator=(const UniqueSocket&) = delete;

    [[nodiscard]] NativeSocket get() const noexcept
    {
        return socket_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return socket_ != kInvalidSocket;
    }

    void reset() noexcept
    {
        if (socket_ != kInvalidSocket)
        {
            native_close(socket_);
            socket_ = kInvalidSocket;
        }
    }

private:
    NativeSocket socket_ = kInvalidSocket;
};

struct WinsockLifetime
{
#ifdef _WIN32
    WinsockLifetime()
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("LoopbackServer: WSAStartup failed");
    }

    ~WinsockLifetime()
    {
        WSACleanup();
    }
#else
    WinsockLifetime() = default;
#endif

    WinsockLifetime(const WinsockLifetime&) = delete;
    WinsockLifetime& operator=(const WinsockLifetime&) = delete;
};

UniqueSocket listen_loopback(int family, int& port_out)
{
    NativeSocket s = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalidSocket)
    {
        throw std::runtime_error(
            "LoopbackServer: socket() failed, error=" + std::to_string(last_socket_error()));
    }

    UniqueSocket owned(s);
    set_cloexec(s);
    set_nonblocking(s);

    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));

    if (family == AF_INET6)
    {
        setsockopt(
            s,
            IPPROTO_IPV6,
            IPV6_V6ONLY,
            reinterpret_cast<const char*>(&on),
            sizeof(on));
    }

    if (family == AF_INET)
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0;
        if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1)
            throw std::runtime_error("LoopbackServer: inet_pton(127.0.0.1) failed");
        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            throw std::runtime_error(
                "LoopbackServer: IPv4 bind failed, error=" + std::to_string(last_socket_error()));
        }

        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) != 0)
            throw std::runtime_error("LoopbackServer: IPv4 getsockname failed");
        port_out = ntohs(bound.sin_port);
    }
    else
    {
        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = 0;
        if (inet_pton(AF_INET6, "::1", &addr.sin6_addr) != 1)
            throw std::runtime_error("LoopbackServer: inet_pton(::1) failed");
        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            throw std::runtime_error(
                "LoopbackServer: IPv6 bind failed, error=" + std::to_string(last_socket_error()));
        }

        sockaddr_in6 bound{};
        socklen_t len = sizeof(bound);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) != 0)
            throw std::runtime_error("LoopbackServer: IPv6 getsockname failed");
        port_out = ntohs(bound.sin6_port);
    }

    if (port_out <= 0)
        throw std::runtime_error("LoopbackServer: got invalid ephemeral port");

    if (::listen(s, SOMAXCONN) != 0)
    {
        throw std::runtime_error(
            "LoopbackServer: listen failed, error=" + std::to_string(last_socket_error()));
    }

    return owned;
}

} // namespace

struct LoopbackServer::Impl
{
    struct Connection
    {
        UniqueSocket socket;
        std::vector<std::uint8_t> received;
        std::vector<std::uint8_t> pending_send;
        bool read_closed = false;
    };

    WinsockLifetime winsock;
    UniqueSocket listen4;
    UniqueSocket listen6;
    int port4 = 0;
    int port6 = 0;
    std::atomic<bool> stop_flag{false};
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::vector<Connection> connections;
    std::thread io_thread;

    Impl()
    {
        listen4 = listen_loopback(AF_INET, port4);
        listen6 = listen_loopback(AF_INET6, port6);
        io_thread = std::thread([this] { run(); });
    }

    ~Impl()
    {
        stop();
    }

    void stop()
    {
        stop_flag.store(true, std::memory_order_release);
        if (io_thread.joinable())
            io_thread.join();

        std::lock_guard lock(mutex);
        listen4.reset();
        listen6.reset();
        for (auto& connection : connections)
            connection.socket.reset();
    }

    void run()
    {
        while (!stop_flag.load(std::memory_order_acquire))
        {
            fd_set read_fds;
            fd_set write_fds;
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);

            NativeSocket max_fd = 0;
            auto add_read = [&](NativeSocket s) {
                if (s == kInvalidSocket)
                    return;
                FD_SET(s, &read_fds);
#ifndef _WIN32
                if (s > max_fd)
                    max_fd = s;
#endif
            };
            auto add_write = [&](NativeSocket s) {
                if (s == kInvalidSocket)
                    return;
                FD_SET(s, &write_fds);
#ifndef _WIN32
                if (s > max_fd)
                    max_fd = s;
#endif
            };

            {
                std::lock_guard lock(mutex);
                add_read(listen4.get());
                add_read(listen6.get());
                for (auto& connection : connections)
                {
                    if (!connection.socket.valid())
                        continue;
                    if (!connection.read_closed)
                        add_read(connection.socket.get());
                    if (!connection.pending_send.empty())
                        add_write(connection.socket.get());
                }
            }

            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000;

            int ready = ::select(
                static_cast<int>(max_fd + 1),
                &read_fds,
                &write_fds,
                nullptr,
                &timeout);
            if (stop_flag.load(std::memory_order_acquire))
                break;
            if (ready < 0)
            {
                const int err = last_socket_error();
                if (is_retryable(err))
                    continue;
                break;
            }
            if (ready == 0)
                continue;

            accept_if_ready(listen4, read_fds);
            accept_if_ready(listen6, read_fds);
            service_connections(read_fds, write_fds);
        }
    }

    void accept_if_ready(UniqueSocket& listener, fd_set& read_fds)
    {
        if (!listener.valid() || !FD_ISSET(listener.get(), &read_fds))
            return;

        NativeSocket accepted = ::accept(listener.get(), nullptr, nullptr);
        if (accepted == kInvalidSocket)
            return;

        set_cloexec(accepted);
        set_nonblocking(accepted);

        {
            std::lock_guard lock(mutex);
            connections.push_back(Connection{UniqueSocket(accepted), {}, {}, false});
        }
        cv.notify_all();
    }

    void service_connections(fd_set& read_fds, fd_set& write_fds)
    {
        std::lock_guard lock(mutex);
        for (auto& connection : connections)
        {
            if (!connection.socket.valid())
                continue;

            NativeSocket s = connection.socket.get();
            if (!connection.read_closed && FD_ISSET(s, &read_fds))
                read_from(connection);
            if (connection.socket.valid() && !connection.pending_send.empty() && FD_ISSET(s, &write_fds))
                write_to(connection);
        }
    }

    void read_from(Connection& connection)
    {
        std::uint8_t buffer[4096];
        int n = ::recv(
            connection.socket.get(),
            reinterpret_cast<char*>(buffer),
            sizeof(buffer),
            0);
        if (n > 0)
        {
            connection.received.insert(
                connection.received.end(),
                buffer,
                buffer + static_cast<std::size_t>(n));
            cv.notify_all();
            return;
        }
        if (n == 0)
        {
            connection.read_closed = true;
            return;
        }
        if (!is_retryable(last_socket_error()))
            connection.read_closed = true;
    }

    void write_to(Connection& connection)
    {
        auto& pending = connection.pending_send;
        const int chunk = static_cast<int>(
            std::min(pending.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
        int n = native_send(
            connection.socket.get(),
            reinterpret_cast<const char*>(pending.data()),
            chunk);
        if (n > 0)
        {
            pending.erase(pending.begin(), pending.begin() + static_cast<std::size_t>(n));
            return;
        }
        if (n == 0 || is_retryable(last_socket_error()))
            return;
        connection.socket.reset();
    }
};

LoopbackServer::LoopbackServer() : impl_(std::make_unique<Impl>()) {}

LoopbackServer::~LoopbackServer() = default;

int LoopbackServer::ipv4_port() const noexcept
{
    return impl_->port4;
}

int LoopbackServer::ipv6_port() const noexcept
{
    return impl_->port6;
}

bool LoopbackServer::wait_for_connection(std::chrono::milliseconds timeout)
{
    return wait_for_connections(1, timeout);
}

bool LoopbackServer::wait_for_connections(std::size_t count, std::chrono::milliseconds timeout)
{
    std::unique_lock lock(impl_->mutex);
    return impl_->cv.wait_for(lock, timeout, [&] {
        return impl_->connections.size() >= count;
    });
}

std::size_t LoopbackServer::connection_count() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->connections.size();
}

std::vector<std::uint8_t> LoopbackServer::received(std::size_t index) const
{
    std::lock_guard lock(impl_->mutex);
    if (index >= impl_->connections.size())
        return {};
    return impl_->connections[index].received;
}

bool LoopbackServer::wait_until_received(
    std::size_t min_bytes,
    std::chrono::milliseconds timeout,
    std::size_t index)
{
    std::unique_lock lock(impl_->mutex);
    return impl_->cv.wait_for(lock, timeout, [&] {
        return index < impl_->connections.size()
            && impl_->connections[index].received.size() >= min_bytes;
    });
}

void LoopbackServer::send(std::span<const std::uint8_t> bytes, std::size_t index)
{
    std::lock_guard lock(impl_->mutex);
    if (index >= impl_->connections.size())
        throw std::out_of_range("LoopbackServer::send: no such connection");
    auto& pending = impl_->connections[index].pending_send;
    pending.insert(pending.end(), bytes.begin(), bytes.end());
}

void LoopbackServer::send(std::string_view bytes, std::size_t index)
{
    send(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(bytes.data()),
            bytes.size()),
        index);
}

void LoopbackServer::stop()
{
    impl_->stop();
}
