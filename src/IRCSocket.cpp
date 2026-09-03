/*
 * Copyright (C) 2011 Fredi Machado <https://github.com/fredimachado>
 * IRCClient is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * http://www.gnu.org/licenses/lgpl.html
 */

#include "IRCSocket.h"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

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
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace
{
constexpr int kMaxDataSize = 4096;
constexpr int kConnectTimeoutMs = 10000;

#ifdef _WIN32
using NativeFd = SOCKET;
constexpr NativeFd kInvalidFd = INVALID_SOCKET;
constexpr int kShutdownBoth = SD_BOTH;
#else
using NativeFd = int;
constexpr NativeFd kInvalidFd = -1;
constexpr int kShutdownBoth = SHUT_RDWR;
#endif

#ifdef _WIN32
std::mutex g_winsock_mutex;
int g_winsock_refs = 0;
#endif

thread_local int t_last_error = 0;

bool acquire_winsock()
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_winsock_mutex);
    if (g_winsock_refs == 0)
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;
    }
    ++g_winsock_refs;
#endif
    return true;
}

void release_winsock()
{
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_winsock_mutex);
    if (g_winsock_refs > 0 && --g_winsock_refs == 0)
        WSACleanup();
#endif
}

int query_last_error()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

void record_native_error()
{
    t_last_error = query_last_error();
}

bool native_error_is_retryable(int err)
{
#ifdef _WIN32
    return err == WSAEINTR || err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
#else
    return err == EINTR || err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS;
#endif
}

void native_close(NativeFd fd)
{
    if (fd == kInvalidFd)
        return;
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

void set_cloexec(NativeFd fd)
{
#ifdef _WIN32
    SetHandleInformation(reinterpret_cast<HANDLE>(fd), HANDLE_FLAG_INHERIT, 0);
#else
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
}

bool set_blocking(NativeFd fd, bool blocking)
{
#ifdef _WIN32
    u_long mode = blocking ? 0ul : 1ul;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) == 0;
#endif
}

void apply_client_options(NativeFd fd)
{
    set_cloexec(fd);
    set_blocking(fd, true);

#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&on), sizeof(on));
#endif
}

int poll_for_connect(NativeFd fd, int timeout_ms)
{
    for (;;)
    {
#ifdef _WIN32
        WSAPOLLFD pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int rc = WSAPoll(&pfd, 1, timeout_ms);
#else
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int rc = ::poll(&pfd, 1, timeout_ms);
#endif
        if (rc < 0 && native_error_is_retryable(query_last_error()))
            continue;
        return rc;
    }
}

int timed_connect(NativeFd fd, const sockaddr* addr, socklen_t addrlen, int timeout_ms)
{
    if (!set_blocking(fd, false))
        return -1;

    int rc = ::connect(fd, addr, addrlen);
    if (rc == 0)
    {
        set_blocking(fd, true);
        return 0;
    }

    const int err = query_last_error();
#ifdef _WIN32
    const bool in_progress = (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS);
#else
    const bool in_progress = (err == EINPROGRESS || err == EWOULDBLOCK);
#endif
    if (!in_progress)
    {
        t_last_error = err;
        set_blocking(fd, true);
        return -1;
    }

    rc = poll_for_connect(fd, timeout_ms);
    if (rc <= 0)
    {
        if (rc == 0)
        {
#ifdef _WIN32
            t_last_error = WSAETIMEDOUT;
#else
            t_last_error = ETIMEDOUT;
#endif
        }
        else
        {
            record_native_error();
        }
        set_blocking(fd, true);
        return -1;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &len) != 0)
    {
        record_native_error();
        set_blocking(fd, true);
        return -1;
    }

    set_blocking(fd, true);
    if (so_error != 0)
    {
        t_last_error = so_error;
        return -1;
    }
    return 0;
}

int native_send(NativeFd fd, const char* buffer, std::size_t length)
{
    const int chunk = static_cast<int>(
        (std::min)(length, static_cast<std::size_t>((std::numeric_limits<int>::max)())));

#ifdef _WIN32
    return ::send(fd, buffer, chunk, 0);
#elif defined(MSG_NOSIGNAL)
    return static_cast<int>(::send(fd, buffer, static_cast<std::size_t>(chunk), MSG_NOSIGNAL));
#else
    return static_cast<int>(::send(fd, buffer, static_cast<std::size_t>(chunk), 0));
#endif
}

int native_recv(NativeFd fd, char* buffer, std::size_t length)
{
    const int chunk = static_cast<int>(
        (std::min)(length, static_cast<std::size_t>((std::numeric_limits<int>::max)())));

#ifdef _WIN32
    return ::recv(fd, buffer, chunk, 0);
#else
    return static_cast<int>(::recv(fd, buffer, static_cast<std::size_t>(chunk), 0));
#endif
}

class NativeSocketOps final : public SocketOps
{
public:
    NativeSocketOps()
        : ready_(acquire_winsock())
        , acquired_(ready_)
    {
    }

    ~NativeSocketOps() override
    {
        std::unordered_map<handle_type, NativeFd> leftover;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            leftover.swap(table_);
        }
        for (auto& entry : leftover)
            native_close(entry.second);

        if (acquired_)
            release_winsock();
    }

    handle_type create() override
    {
        if (!ready_)
            return invalid_handle;

        std::lock_guard<std::mutex> lock(mutex_);
        handle_type id = next_id_++;
        if (id == invalid_handle)
            id = next_id_++;
        table_.emplace(id, kInvalidFd);
        return id;
    }

    int connect(handle_type socket, std::string_view host, int port) override
    {
        if (!ready_ || host.empty() || port <= 0 || port > 65535)
            return -1;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (table_.find(socket) == table_.end())
                return -1;
        }

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        const std::string host_str(host);
        const std::string port_str = std::to_string(port);

        addrinfo* result = nullptr;
        if (getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &result) != 0 || !result)
        {
            record_native_error();
            return -1;
        }

        int rc = -1;
        for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next)
        {
            NativeFd fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd == kInvalidFd)
            {
                record_native_error();
                continue;
            }

            apply_client_options(fd);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = table_.find(socket);
                if (it == table_.end())
                {
                    native_close(fd);
                    freeaddrinfo(result);
                    return -1;
                }
                if (it->second != kInvalidFd)
                    native_close(it->second);
                it->second = fd;
            }

            if (timed_connect(
                    fd,
                    ai->ai_addr,
                    static_cast<socklen_t>(ai->ai_addrlen),
                    kConnectTimeoutMs) == 0)
            {
                rc = 0;
                break;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = table_.find(socket);
                if (it == table_.end())
                {
                    freeaddrinfo(result);
                    return -1;
                }
                if (it->second == fd)
                    it->second = kInvalidFd;
                else
                    fd = kInvalidFd;
            }
            native_close(fd);
        }

        freeaddrinfo(result);
        return rc;
    }

    int send(handle_type socket, const char* buffer, std::size_t length) override
    {
        NativeFd fd = lookup(socket);
        if (fd == kInvalidFd || buffer == nullptr)
            return -1;

        const int n = native_send(fd, buffer, length);
        if (n < 0)
            record_native_error();
        return n;
    }

    int recv(handle_type socket, char* buffer, std::size_t length) override
    {
        NativeFd fd = lookup(socket);
        if (fd == kInvalidFd || buffer == nullptr)
            return -1;

        const int n = native_recv(fd, buffer, length);
        if (n < 0)
            record_native_error();
        return n;
    }

    int shutdown(handle_type socket) override
    {
        NativeFd fd = lookup(socket);
        if (fd == kInvalidFd)
            return -1;

        const int rc = ::shutdown(fd, kShutdownBoth);
        if (rc != 0)
            record_native_error();
        return rc;
    }

    int close(handle_type socket) override
    {
        NativeFd fd = kInvalidFd;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = table_.find(socket);
            if (it == table_.end())
                return -1;
            fd = it->second;
            table_.erase(it);
        }
        native_close(fd);
        return 0;
    }

    [[nodiscard]] bool last_error_is_retryable() const override
    {
        return native_error_is_retryable(t_last_error);
    }

private:
    NativeFd lookup(handle_type socket)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_.find(socket);
        if (it == table_.end())
            return kInvalidFd;
        return it->second;
    }

    std::mutex mutex_;
    std::unordered_map<handle_type, NativeFd> table_;
    handle_type next_id_ = 1;
    bool ready_ = false;
    bool acquired_ = false;
};

enum class SocketState
{
    uninitialized,
    disconnected,
    connecting,
    connected,
    stopping
};

} // namespace

struct IRCSocket::Impl
{
    Impl()
        : owned_ops(std::make_unique<NativeSocketOps>())
        , ops(owned_ops.get())
    {
    }

    explicit Impl(SocketOps& external)
        : ops(&external)
    {
    }

    ~Impl()
    {
        if (ops != nullptr && socket != SocketOps::invalid_handle)
        {
            ops->close(socket);
            socket = SocketOps::invalid_handle;
        }
    }

    bool ensure_initialized()
    {
        if (state != SocketState::uninitialized)
            return true;
        if (!acquire_winsock())
            return false;
        winsock_held = true;
        state = SocketState::disconnected;
        return true;
    }

    void begin_stop()
    {
        if (state == SocketState::disconnected || state == SocketState::uninitialized)
            return;
        state = SocketState::stopping;
        if (ops != nullptr && socket != SocketOps::invalid_handle)
            ops->shutdown(socket);
        cv.notify_all();
    }

    void close_handle()
    {
        if (ops == nullptr || socket == SocketOps::invalid_handle)
            return;
        const SocketOps::handle_type handle = socket;
        socket = SocketOps::invalid_handle;
        ops->close(handle);
    }

    void wait_idle_and_close(std::unique_lock<std::mutex>& lock)
    {
        cv.wait(lock, [this] { return in_flight == 0; });
        close_handle();
        state = SocketState::disconnected;
    }

    void disconnect_locked(std::unique_lock<std::mutex>& lock)
    {
        if (state == SocketState::uninitialized || state == SocketState::disconnected)
            return;
        begin_stop();
        wait_idle_and_close(lock);
    }

    std::unique_ptr<SocketOps> owned_ops;
    SocketOps* ops = nullptr;
    mutable std::mutex mutex;
    std::mutex send_mutex;
    std::condition_variable cv;
    SocketState state = SocketState::uninitialized;
    SocketOps::handle_type socket = SocketOps::invalid_handle;
    int in_flight = 0;
    bool winsock_held = false;
};

IRCSocket::IRCSocket()
    : impl_(std::make_unique<Impl>())
{
}

IRCSocket::IRCSocket(SocketOps& ops)
    : impl_(std::make_unique<Impl>(ops))
{
}

IRCSocket::~IRCSocket()
{
    Disconnect();
    if (impl_ && impl_->winsock_held)
        release_winsock();
}

bool IRCSocket::Init()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != SocketState::uninitialized)
        return true;
    if (!impl_->ensure_initialized())
    {
        std::cout << "Unable to initialize Winsock." << std::endl;
        return false;
    }
    return true;
}

bool IRCSocket::Connect(char const* host, int port)
{
    if (host == nullptr || port <= 0 || port > 65535)
        return false;

    Disconnect();

    SocketOps::handle_type handle = SocketOps::invalid_handle;
    {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        if (!impl_->ensure_initialized())
        {
            std::cout << "Unable to initialize Winsock." << std::endl;
            return false;
        }
        if (impl_->state != SocketState::disconnected)
            return false;

        impl_->state = SocketState::connecting;
        handle = impl_->ops->create();
        if (handle == SocketOps::invalid_handle)
        {
            impl_->state = SocketState::disconnected;
            std::cout << "Socket error." << std::endl;
            return false;
        }
        impl_->socket = handle;
        ++impl_->in_flight;
    }

    const int rc = impl_->ops->connect(handle, std::string_view(host), port);

    std::unique_lock<std::mutex> lock(impl_->mutex);
    --impl_->in_flight;
    impl_->cv.notify_all();

    if (impl_->state == SocketState::stopping)
    {
        impl_->wait_idle_and_close(lock);
        return false;
    }

    if (rc != 0)
    {
        impl_->close_handle();
        impl_->state = SocketState::disconnected;
        std::cout << "Could not connect to: " << host << std::endl;
        return false;
    }

    impl_->state = SocketState::connected;
    return true;
}

void IRCSocket::Disconnect()
{
    std::lock_guard<std::mutex> send_lock(impl_->send_mutex);
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->disconnect_locked(lock);
}

bool IRCSocket::Connected() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state == SocketState::connected;
}

bool IRCSocket::SendData(char const* data)
{
    if (data == nullptr)
        return false;

    const std::size_t length = std::strlen(data);

    std::lock_guard<std::mutex> send_lock(impl_->send_mutex);

    SocketOps::handle_type handle = SocketOps::invalid_handle;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state != SocketState::connected)
            return false;
        handle = impl_->socket;
        ++impl_->in_flight;
    }

    bool ok = true;
    std::size_t offset = 0;
    while (offset < length)
    {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->state != SocketState::connected)
            {
                ok = false;
                break;
            }
        }

        const int n = impl_->ops->send(handle, data + offset, length - offset);
        if (n < 0)
        {
            if (impl_->ops->last_error_is_retryable())
            {
                std::this_thread::yield();
                continue;
            }
            ok = false;
            break;
        }
        if (n == 0)
        {
            ok = false;
            break;
        }
        offset += static_cast<std::size_t>(n);
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        --impl_->in_flight;
        impl_->cv.notify_all();
    }

    return ok && offset == length;
}

std::string IRCSocket::ReceiveData()
{
    SocketOps::handle_type handle = SocketOps::invalid_handle;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->state != SocketState::connected)
            return {};
        handle = impl_->socket;
        ++impl_->in_flight;
    }

    char buffer[kMaxDataSize];
    int bytes = 0;
    for (;;)
    {
        bytes = impl_->ops->recv(handle, buffer, sizeof(buffer));
        if (bytes < 0 && impl_->ops->last_error_is_retryable())
        {
            std::this_thread::yield();
            continue;
        }
        break;
    }

    std::unique_lock<std::mutex> lock(impl_->mutex);
    --impl_->in_flight;
    impl_->cv.notify_all();

    if (impl_->state != SocketState::connected)
        return {};

    if (bytes > 0)
        return std::string(buffer, static_cast<std::size_t>(bytes));

    impl_->disconnect_locked(lock);
    return {};
}
