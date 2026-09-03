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

#include "ConsoleInput.h"

#include <csignal>
#include <cstddef>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace
{
bool extract_line(std::string& buffer, std::string& line)
{
    auto const pos = buffer.find('\n');
    if (pos == std::string::npos)
        return false;

    line.assign(buffer, 0, pos);
    buffer.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return true;
}

#ifdef _WIN32
void close_handle(HANDLE& handle)
{
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
        handle = nullptr;
    }
}
#else
void close_fd(int& fd)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
}

void set_cloexec(int fd)
{
    int const flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void set_nonblock(int fd)
{
    int const flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
#endif
}

struct ConsoleInput::Impl
{
    volatile std::sig_atomic_t stop_flag = 0;
    bool ready = false;
    std::string buffer;

#ifdef _WIN32
    HANDLE wake_event = nullptr;
    HANDLE stdin_handle = INVALID_HANDLE_VALUE;
    HANDLE reader_thread = nullptr;
    DWORD stdin_type = FILE_TYPE_UNKNOWN;

    enum class PipeReadStatus
    {
        Pending,
        DataRead,
        Finished
    };

    void bind_reader_thread()
    {
        if (reader_thread != nullptr)
            return;

        HANDLE duplicated = nullptr;
        if (DuplicateHandle(
                GetCurrentProcess(),
                GetCurrentThread(),
                GetCurrentProcess(),
                &duplicated,
                0,
                FALSE,
                DUPLICATE_SAME_ACCESS))
        {
            reader_thread = duplicated;
        }
    }

    ConsoleInput::ReadResult finish_eof()
    {
        if (!buffer.empty())
        {
            ConsoleInput::ReadResult result;
            result.status = ConsoleInput::Status::Line;
            result.line = std::move(buffer);
            buffer.clear();
            if (!result.line.empty() && result.line.back() == '\r')
                result.line.pop_back();
            return result;
        }

        ConsoleInput::ReadResult result;
        result.status = ConsoleInput::Status::Eof;
        return result;
    }

    PipeReadStatus read_available_pipe(ConsoleInput::ReadResult& result)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(stdin_handle, nullptr, 0, nullptr, &available, nullptr))
        {
            DWORD const err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_NO_DATA)
            {
                result = finish_eof();
                return PipeReadStatus::Finished;
            }
            result.status = ConsoleInput::Status::Error;
            return PipeReadStatus::Finished;
        }

        if (available == 0)
            return PipeReadStatus::Pending;

        char buf[1024];
        DWORD const to_read = available > sizeof(buf) ? static_cast<DWORD>(sizeof(buf)) : available;
        DWORD n = 0;
        if (!ReadFile(stdin_handle, buf, to_read, &n, nullptr))
        {
            DWORD const err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_OPERATION_ABORTED)
            {
                result = stop_flag != 0
                    ? ConsoleInput::ReadResult{ConsoleInput::Status::Stopped, {}}
                    : finish_eof();
                return PipeReadStatus::Finished;
            }
            result.status = ConsoleInput::Status::Error;
            return PipeReadStatus::Finished;
        }
        if (n == 0)
        {
            result = finish_eof();
            return PipeReadStatus::Finished;
        }
        buffer.append(buf, buf + n);
        return PipeReadStatus::DataRead;
    }
#else
    int wake_read = -1;
    int wake_write = -1;
#endif

    ~Impl()
    {
#ifdef _WIN32
        close_handle(reader_thread);
        close_handle(wake_event);
#else
        close_fd(wake_read);
        close_fd(wake_write);
#endif
    }
};

ConsoleInput::ConsoleInput()
    : impl_(std::make_unique<Impl>())
{
#ifdef _WIN32
    impl_->wake_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl_->stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    impl_->stdin_type = GetFileType(impl_->stdin_handle);
    impl_->ready = impl_->wake_event != nullptr
        && impl_->stdin_handle != nullptr
        && impl_->stdin_handle != INVALID_HANDLE_VALUE;
#else
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0)
        return;
    impl_->wake_read = fds[0];
    impl_->wake_write = fds[1];
    set_cloexec(impl_->wake_read);
    set_cloexec(impl_->wake_write);
    set_nonblock(impl_->wake_read);
    set_nonblock(impl_->wake_write);
    impl_->ready = true;
#endif
}

ConsoleInput::~ConsoleInput() = default;

bool ConsoleInput::valid() const
{
    return impl_ && impl_->ready;
}

bool ConsoleInput::stop_requested() const
{
    return impl_ && impl_->stop_flag != 0;
}

void ConsoleInput::wake()
{
    if (!impl_ || !impl_->ready)
        return;

#ifdef _WIN32
    SetEvent(impl_->wake_event);
#else
    char byte = 1;
    (void)::write(impl_->wake_write, &byte, 1);
#endif
}

void ConsoleInput::request_stop()
{
    if (!impl_)
        return;
    impl_->stop_flag = 1;
    wake();
#ifdef _WIN32
    HANDLE const thread = impl_->reader_thread;
    if (thread != nullptr)
        CancelSynchronousIo(thread);
#endif
}

ConsoleInput::ReadResult ConsoleInput::read_line()
{
    ReadResult result;
    if (!valid())
    {
        result.status = Status::Error;
        return result;
    }

#ifdef _WIN32
    impl_->bind_reader_thread();
#endif

    for (;;)
    {
        if (impl_->stop_flag != 0)
        {
            result.status = Status::Stopped;
            return result;
        }

        if (extract_line(impl_->buffer, result.line))
        {
            result.status = Status::Line;
            return result;
        }

#ifdef _WIN32
        if (impl_->stdin_type == FILE_TYPE_CHAR)
        {
            HANDLE handles[2] = {impl_->stdin_handle, impl_->wake_event};
            DWORD const wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (impl_->stop_flag != 0)
            {
                result.status = Status::Stopped;
                return result;
            }
            if (wait == WAIT_OBJECT_0 + 1)
            {
                result.status = Status::Stopped;
                return result;
            }
            if (wait != WAIT_OBJECT_0)
            {
                result.status = Status::Error;
                return result;
            }

            char buf[1024];
            DWORD n = 0;
            if (!ReadFile(impl_->stdin_handle, buf, sizeof(buf), &n, nullptr))
            {
                DWORD const err = GetLastError();
                if (err == ERROR_OPERATION_ABORTED || impl_->stop_flag != 0)
                {
                    result.status = Status::Stopped;
                    return result;
                }
                result.status = Status::Error;
                return result;
            }
            if (n == 0)
                return impl_->finish_eof();
            impl_->buffer.append(buf, buf + n);
            continue;
        }

        if (impl_->stdin_type == FILE_TYPE_PIPE)
        {
            Impl::PipeReadStatus const read_status = impl_->read_available_pipe(result);
            if (read_status == Impl::PipeReadStatus::Finished)
                return result;
            if (read_status == Impl::PipeReadStatus::DataRead)
                continue;
            DWORD const wait = WaitForSingleObject(impl_->wake_event, 25);
            if (impl_->stop_flag != 0 || wait == WAIT_OBJECT_0)
            {
                result.status = Status::Stopped;
                return result;
            }
            continue;
        }

        char buf[1024];
        DWORD n = 0;
        if (!ReadFile(impl_->stdin_handle, buf, sizeof(buf), &n, nullptr))
        {
            if (impl_->stop_flag != 0 || GetLastError() == ERROR_OPERATION_ABORTED)
            {
                result.status = Status::Stopped;
                return result;
            }
            result.status = Status::Error;
            return result;
        }
        if (n == 0)
            return impl_->finish_eof();
        impl_->buffer.append(buf, buf + n);
#else
        pollfd fds[2]{};
        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[1].fd = impl_->wake_read;
        fds[1].events = POLLIN;

        int const rc = ::poll(fds, 2, -1);
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;
            result.status = Status::Error;
            return result;
        }

        if (impl_->stop_flag != 0 || (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
        {
            char drain[32];
            while (::read(impl_->wake_read, drain, sizeof(drain)) > 0)
            {
            }
            result.status = Status::Stopped;
            return result;
        }

        if ((fds[0].revents & (POLLNVAL | POLLERR)) != 0)
        {
            result.status = Status::Error;
            return result;
        }

        if ((fds[0].revents & (POLLIN | POLLHUP)) != 0)
        {
            char buf[1024];
            ssize_t const n = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                result.status = Status::Error;
                return result;
            }
            if (n == 0)
            {
                if (!impl_->buffer.empty())
                {
                    result.status = Status::Line;
                    result.line = std::move(impl_->buffer);
                    impl_->buffer.clear();
                    if (!result.line.empty() && result.line.back() == '\r')
                        result.line.pop_back();
                    return result;
                }
                result.status = Status::Eof;
                return result;
            }
            impl_->buffer.append(buf, buf + n);
        }
#endif
    }
}
