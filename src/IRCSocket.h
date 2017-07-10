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

#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib,"WS2_32")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket(s) close(s)
#define close(s)
#define SOCKET_ERROR -1
#define INVALID_SOCKET -1
#endif

class IRCSocket
{
public:
    bool Init();

    bool Connect(char const* host, int port);
    void Disconnect();

    bool Connected() { return _connected; };

    bool SendData(char const* data);
    std::string ReceiveData();

private:
    int _socket;

    bool _connected;
};

#endif
