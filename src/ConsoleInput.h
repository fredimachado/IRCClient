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

#ifndef IRCCLIENT_CONSOLE_INPUT_H
#define IRCCLIENT_CONSOLE_INPUT_H

#include <memory>
#include <string>

// Interruptible stdin reader for interactive terminals and redirected pipes
// on Windows and POSIX. A stop token is not enough: POSIX uses a self-pipe
// and Windows waits on the console handle plus a wake event (with
// CancelSynchronousIo on a blocking read). request_stop() / wake() only
// touch a sig_atomic_t flag and the native wake primitive, so they can be
// called from a SIGINT path. stdin EOF is a clean stop, not an error loop.
class ConsoleInput
{
public:
    enum class Status
    {
        Line,
        Eof,
        Stopped,
        Error
    };

    struct ReadResult
    {
        Status status = Status::Error;
        std::string line;
    };

    ConsoleInput();
    ~ConsoleInput();

    ConsoleInput(const ConsoleInput&) = delete;
    ConsoleInput& operator=(const ConsoleInput&) = delete;
    ConsoleInput(ConsoleInput&&) = delete;
    ConsoleInput& operator=(ConsoleInput&&) = delete;

    [[nodiscard]] bool valid() const;

    // Blocks until a complete line, EOF, stop, or error.
    ReadResult read_line();

    // Async-signal-safe on POSIX. Safe from a Windows SIGINT / console-control thread.
    void request_stop();
    void wake();

    [[nodiscard]] bool stop_requested() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
