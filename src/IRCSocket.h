/**
 * IRC Socket
 *
 * Fredi Machado
 * http://github.com/Fredi
 */

#ifndef _IRCSOCKET_H
#define _IRCSOCKET_H

#include <iostream>
#include <sstream>
#include "winsock2.h"

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
    SOCKET _socket;

    bool _connected;
};

#endif
