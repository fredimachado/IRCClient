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

#ifndef _SOCKETOPS_H
#define _SOCKETOPS_H

#include <cstddef>
#include <cstdint>
#include <string_view>

// Injectable socket operations. Tests can fake partial sends, retryable
// errors, EOF, hard errors, and handle reuse after close. Production code
// will wrap native sockets. handle_type matches SOCKET width on Windows
// (UINT_PTR) and int on POSIX. No TLS.
class SocketOps
{
public:
#ifdef _WIN32
    using handle_type = std::uintptr_t;
#else
    using handle_type = int;
#endif

    static constexpr handle_type invalid_handle = static_cast<handle_type>(-1);

    virtual ~SocketOps() = default;

    SocketOps(const SocketOps&) = delete;
    SocketOps& operator=(const SocketOps&) = delete;
    SocketOps(SocketOps&&) = delete;
    SocketOps& operator=(SocketOps&&) = delete;

    // Create a new handle. After close(), a later create() may return the
    // same value so callers must not use a closed handle.
    [[nodiscard]] virtual handle_type create() = 0;

    // Connect an existing handle to host:port. Returns 0 on success, -1 on
    // error. Check last_error_is_retryable() after a failure.
    virtual int connect(handle_type socket, std::string_view host, int port) = 0;

    // Returns bytes sent, or a negative value on error. A short count is a
    // partial send, not success. A retryable error means try again.
    virtual int send(handle_type socket, const char* buffer, std::size_t length) = 0;

    // Returns bytes received, 0 on EOF, or a negative value on error.
    virtual int recv(handle_type socket, char* buffer, std::size_t length) = 0;

    // Unblocks a pending recv. Returns 0 on success, -1 on error.
    virtual int shutdown(handle_type socket) = 0;

    // Release the handle exactly once. Returns 0 on success, -1 on error.
    virtual int close(handle_type socket) = 0;

    // True when the last failed send/recv/connect should be retried
    // (EINTR, EAGAIN, EWOULDBLOCK, and Windows equivalents).
    [[nodiscard]] virtual bool last_error_is_retryable() const = 0;

protected:
    SocketOps() = default;
};

#endif
