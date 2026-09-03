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

#ifndef IRCCLIENT_CONSOLE_INTERRUPT_H
#define IRCCLIENT_CONSOLE_INTERRUPT_H

#include <functional>
#include <memory>

// RAII OS interrupt watch. POSIX blocks SIGINT in the constructing thread
// before workers are created and delivers it on a sigwait thread, so no
// C++ object is touched from an asynchronous signal handler. Windows
// installs a SetConsoleCtrlHandler and notifies from that control thread.
// request() is the same path as a real interrupt and is idempotent.
class ConsoleInterrupt
{
public:
    explicit ConsoleInterrupt(std::function<void()> on_interrupt);
    ~ConsoleInterrupt();

    ConsoleInterrupt(const ConsoleInterrupt&) = delete;
    ConsoleInterrupt& operator=(const ConsoleInterrupt&) = delete;
    ConsoleInterrupt(ConsoleInterrupt&&) = delete;
    ConsoleInterrupt& operator=(ConsoleInterrupt&&) = delete;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool interrupted() const;

    void request();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
