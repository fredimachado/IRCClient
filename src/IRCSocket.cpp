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

#include <cstring>
#include <fcntl.h>
#include "IRCSocket.h"

#define MAXDATASIZE 4096

bool IRCSocket::Init()
{
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData))
    {
        std::cout << "Unable to initialize Winsock." << std::endl;
        return false;
    }
    #endif

    if ((_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
    {
        std::cout << "Socket error." << std::endl;
        #ifdef _WIN32
        WSACleanup();
        #endif
        return false;
    }

    int on = 1;
    if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, (char const*)&on, sizeof(on)) == -1)
    {
        std::cout << "Invalid socket." << std::endl;
        #ifdef _WIN32
        WSACleanup();
        #endif
        return false;
    }

    #ifdef _WIN32
    u_long mode = 0;
    ioctlsocket(_socket, FIONBIO, &mode);
    #else
    fcntl(_socket, F_SETFL, O_NONBLOCK);
    fcntl(_socket, F_SETFL, O_ASYNC);
    #endif

    return true;
}

bool IRCSocket::Connect(char const* host, int port)
{
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = NULL;
    std::ostringstream portStr;
    portStr << port;

    if (getaddrinfo(host, portStr.str().c_str(), &hints, &result) != 0 || !result)
    {
        std::cout << "Could not resolve host: " << host << std::endl;
        #ifdef _WIN32
        WSACleanup();
        #endif
        return false;
    }

    int connectResult = connect(_socket, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);

    if (connectResult == SOCKET_ERROR)
    {
        std::cout << "Could not connect to: " << host << std::endl;
        closesocket(_socket);
        return false;
    }

    _connected = true;

    return true;
}

void IRCSocket::Disconnect()
{
    if (_connected)
    {
        #ifdef _WIN32
        shutdown(_socket, 2);
        #endif
        closesocket(_socket);
        _connected = false;
    }
}

bool IRCSocket::SendData(char const* data)
{
    if (_connected)
        if (send(_socket, data, strlen(data), 0) == -1)
            return false;

    return true;
}

std::string IRCSocket::ReceiveData()
{
    char buffer[MAXDATASIZE];

    memset(buffer, 0, MAXDATASIZE);

    int bytes = recv(_socket, buffer, MAXDATASIZE - 1, 0);

    if (bytes > 0)
        return std::string(buffer);
    else
        Disconnect();

    return "";
}
