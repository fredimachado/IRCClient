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

#ifndef _IRCSOCKET_H
#define _IRCSOCKET_H

#include "SocketOps.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

class IRCSocket
{
public:
    IRCSocket();
    explicit IRCSocket(SocketOps& ops);
    ~IRCSocket();

    IRCSocket(const IRCSocket&) = delete;
    IRCSocket& operator=(const IRCSocket&) = delete;
    IRCSocket(IRCSocket&&) = delete;
    IRCSocket& operator=(IRCSocket&&) = delete;

    bool Init();
    bool Connect(char const* host, int port);
    void Disconnect();
    bool Connected() const;
    bool SendData(char const* data);
    std::string ReceiveData();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
