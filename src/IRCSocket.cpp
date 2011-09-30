/**
 * IRC Socket
 *
 * TODO: Linux compatibility
 *
 * Fredi Machado
 * http://github.com/Fredi
 */

#include <cstring>
#include <fcntl.h>
#include "IRCSocket.h"

#define MAXDATASIZE 4096

bool IRCSocket::Init()
{
    #ifdef WIN32
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
        #ifdef WIN32
        WSACleanup();
        #endif
        return false;
    }

    int on = 1;
    if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, (char const*)&on, sizeof(on)) == -1)
    {
        std::cout << "Invalid socket." << std::endl;
        #ifdef WIN32
        WSACleanup();
        #endif
        return false;
    }

    #ifdef WIN32
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
    hostent* he;

    if (!(he = gethostbyname(host)))
    {
        std::cout << "Could not resolve host: " << host << std::endl;
        #ifdef WIN32
        WSACleanup();
        #endif
        return false;
    }

    sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *((const in_addr*)he->h_addr);
    memset(&(addr.sin_zero), '\0', 8);

    if (connect(_socket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
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
        #ifdef WIN32
        shutdown(_socket, 2);
        #endif
        closesocket(_socket);
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
    {
        std::cout << "Bytes received: " << bytes << std::endl;

        return std::string(buffer);
    }
    else
    {
        std::cout << "recv failed." << std::endl;

        _connected = false;
    }

    return "";
}
