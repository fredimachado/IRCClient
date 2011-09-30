/*
 * Copyright (C) 2011 Fredi Machado <https://github.com/Fredi>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <signal.h>
#include <cstdlib>
#include "IRCClient.h"

volatile bool running;

void signalHandler(int signal)
{
    running = false;
}

// Join #myircclient channel right after successful logged in
int onLoggedIn(IRCMessage message, void* client)
{
    ((IRCClient*)client)->SendIRC("JOIN #myircclient");

    return 0;
}

int onPrivMsg(IRCMessage message, void* client)
{
    if (message.prefix.nick != "Fredi")
        return 1;

    std::string text = message.parameters.at(message.parameters.size() - 1);

    if (text == "enter channel")
        ((IRCClient*)client)->SendIRC("JOIN #myircclient");
    if (text == "leave channel")
        ((IRCClient*)client)->SendIRC("PART #myircclient");
    if (text == "quit now")
        ((IRCClient*)client)->SendIRC("QUIT :Bye bye!");

    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Insuficient parameters: host port [nick] [user]" << std::endl;
        return 1;
    }

    char* host = argv[1];
    int port = atoi(argv[2]);
    std::string nick("MyIRCClient");
    std::string user("IRCClient");

    if (argc >= 4)
        nick = argv[3];
    if (argc >= 5)
        user = argv[4];

    IRCClient client;

    // Hook command 001
    client.HookIRCCommand("001", &onLoggedIn);
    // Hook PRIVMSG
    client.HookIRCCommand("PRIVMSG", &onPrivMsg);

    if (client.InitSocket())
    {
        std::cout << "Socket initialized. Connecting..." << std::endl;

        if (client.Connect(host, port))
        {
            std::cout << "Connected. Loggin in..." << std::endl;

            if (client.Login(nick, user))
            {
                std::cout << "Logged." << std::endl;

                running = true;
                signal(SIGINT, signalHandler);

                while (client.Connected() && running)
                    client.ReceiveData();
            }

            client.Disconnect();
            std::cout << "Disconnected." << std::endl;
        }
    }
}
