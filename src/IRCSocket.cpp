/**
 * IRC Socket
 *
 * TODO: Linux compatibility
 *
 * Fredi Machado
 * http://github.com/Fredi
 */

#include "IRCSocket.h"

#define MAXDATASIZE 512

bool IRCSocket::Init()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData))
    {
        std::cout << "Unable to initialize Winsock." << std::endl;
        return false;
    }

    if ((_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
    {
        std::cout << "Socket error." << std::endl;
        WSACleanup();
        return false;
    }

    int on = 1;
    if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, (char const*)&on, sizeof(on)) == -1)
    {
        std::cout << "Invalid socket." << std::endl;
        WSACleanup();
        return false;
    }

    u_long mode = 0;
    ioctlsocket(_socket, FIONBIO, &mode);

    return true;
}

bool IRCSocket::Connect(char const* host, int port)
{
    struct hostent* he;

    if (!(he = gethostbyname(host)))
    {
        std::cout << "Could not resolve host: " << host << std::endl;
        WSACleanup();
        return false;
    }

    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *((struct in_addr*)he->h_addr);
    memset(&(addr.sin_zero), '\0', 8);

    if (connect(_socket, (struct sockaddr*)&addr, sizeof(struct sockaddr)) == SOCKET_ERROR)
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
    if (_socket)
    {
        closesocket(_socket);
        WSACleanup();
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
        if (bytes == 0)
            std::cout << "Connection closed." << std::endl;
        else
            std::cout << "recv failed with error: " << WSAGetLastError() << std::endl;

        _connected = false;
    }

    return "";
}
