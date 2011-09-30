/**
 * Simple IRC Client
 *
 * Currently only connecting to the server
 * Press Ctrl+C to quit
 *
 * Fredi Machado
 * http://github.com/Fredi
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

int onPrivMsg(IRCCommandPrefix prefix, std::vector<std::string> parameters, void* client)
{
    if (prefix.nick != "Fredi")
        return 1;

    std::string text = parameters.at(parameters.size() - 1);

    if (text == "enter #trinity")
        ((IRCClient*)client)->SendIRC("JOIN #trinity");
    if (text == "leave #trinity")
        ((IRCClient*)client)->SendIRC("LEAVE #trinity");
    if (text == "quit now")
        ((IRCClient*)client)->SendIRC("QUIT :Bye bye!");

    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cout << "Insuficient parameters: host port" << std::endl;
        return 1;
    }

    char* host = argv[1];
    int port = atoi(argv[2]);

    IRCClient client;

    client.HookIRCCommand("PRIVMSG", &onPrivMsg);

    if (client.InitSocket())
    {
        std::cout << "Socket initialized. Connecting..." << std::endl;

        if (client.Connect(host, port))
        {
            std::cout << "Connected. Loggin in..." << std::endl;

            if (client.Login("MyIRCClient", "IRCClient"))
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
