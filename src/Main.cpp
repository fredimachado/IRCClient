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

#include "ConsoleInput.h"
#include "ConsoleInterrupt.h"
#include "IRCClient.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <stop_token>
#include <string>
#include <syncstream>
#include <system_error>
#include <thread>

namespace
{
    char ToLowerAscii(char ch)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    char ToUpperAscii(char ch)
    {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    bool ParsePort(char const* text, int& port)
    {
        if (text == nullptr || *text == '\0')
            return false;

        char* end = nullptr;
        errno = 0;
        long const value = std::strtol(text, &end, 10);
        if (errno != 0 || end == text || *end != '\0')
            return false;
        if (value <= 0 || value > 65535)
            return false;

        port = static_cast<int>(value);
        return true;
    }

    bool IsQuitCommand(std::string command)
    {
        if (command.empty() || command.front() != '/')
            return false;
        std::transform(command.begin(), command.end(), command.begin(), ToLowerAscii);
        return command == "/quit" || command.rfind("/quit ", 0) == 0;
    }

    class ShutdownCoordinator
    {
    public:
        void request()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (requested_)
                    return;
                requested_ = true;
            }
            cv_.notify_all();
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return requested_; });
        }

        [[nodiscard]] bool requested() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return requested_;
        }

    private:
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        bool requested_ = false;
    };
}

class ConsoleCommandHandler
{
public:
    bool AddCommand(std::string name, int argCount, void (*handler)(std::string /*params*/, IRCClient* /*client*/))
    {
        CommandEntry entry;
        entry.argCount = argCount;
        entry.handler = handler;
        std::transform(name.begin(), name.end(), name.begin(), ToLowerAscii);
        _commands.insert(std::pair<std::string, CommandEntry>(name, entry));
        return true;
    }

    void ParseCommand(std::string command, IRCClient* client)
    {
        if (_commands.empty())
        {
            std::osyncstream(std::cout) << "No commands available." << std::endl;
            return;
        }

        if (command.empty())
            return;

        if (command.front() == '/')
            command = command.substr(1);

        if (command.empty())
            return;

        auto const space = command.find(' ');
        std::string name = command.substr(0, space);
        std::string args = (space == std::string::npos) ? std::string() : command.substr(space + 1);

        std::transform(name.begin(), name.end(), name.begin(), ToLowerAscii);

        auto const itr = _commands.find(name);
        if (itr == _commands.end())
        {
            std::osyncstream(std::cout) << "Command not found." << std::endl;
            return;
        }

        std::ptrdiff_t const spaces = std::count(args.begin(), args.end(), ' ');
        if (spaces + 1 < static_cast<std::ptrdiff_t>(itr->second.argCount))
        {
            std::osyncstream(std::cout) << "Insufficient arguments." << std::endl;
            return;
        }

        (*(itr->second.handler))(args, client);
    }

private:
    struct CommandEntry
    {
        int argCount;
        void (*handler)(std::string /*arguments*/, IRCClient* /*client*/);
    };

    std::map<std::string, CommandEntry> _commands;
};

ConsoleCommandHandler commandHandler;

void msgCommand(std::string arguments, IRCClient* client)
{
    std::string to = arguments.substr(0, arguments.find(' '));
    std::string text = arguments.substr(arguments.find(' ') + 1);

    std::osyncstream(std::cout) << "To " + to + ": " + text << std::endl;
    client->SendIRC("PRIVMSG " + to + " :" + text);
}

void joinCommand(std::string channel, IRCClient* client)
{
    if (channel.empty())
        return;
    if (channel.front() != '#')
        channel = "#" + channel;

    client->SendIRC("JOIN " + channel);
}

void partCommand(std::string channel, IRCClient* client)
{
    if (channel.empty())
        return;
    if (channel.front() != '#')
        channel = "#" + channel;

    client->SendIRC("PART " + channel);
}

void ctcpCommand(std::string arguments, IRCClient* client)
{
    std::string to = arguments.substr(0, arguments.find(' '));
    std::string text = arguments.substr(arguments.find(' ') + 1);

    std::transform(text.begin(), text.end(), text.begin(), ToUpperAscii);

    client->SendIRC("PRIVMSG " + to + " :\001" + text + "\001");
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::osyncstream(std::cerr)
            << "Insufficient parameters: host port [nick] [user] [password]" << std::endl;
        return 1;
    }

    char* host = argv[1];
    int port = 0;
    if (!ParsePort(argv[2], port))
    {
        std::osyncstream(std::cerr) << "Invalid port." << std::endl;
        return 1;
    }

    std::string nick("MyIRCClient");
    std::string user("IRCClient");
    std::string password;

    if (argc >= 4)
        nick = argv[3];
    if (argc >= 5)
        user = argv[4];
    if (argc >= 6)
        password = argv[5];

    IRCClient client;
    client.Debug(true);

    if (!client.InitSocket())
    {
        std::osyncstream(std::cerr) << "Failed to initialize socket." << std::endl;
        return 1;
    }

    std::osyncstream(std::cout) << "Socket initialized. Connecting..." << std::endl;

    if (!client.Connect(host, port))
    {
        std::osyncstream(std::cerr) << "Failed to connect." << std::endl;
        return 1;
    }

    std::osyncstream(std::cout) << "Connected. Logging in..." << std::endl;

    if (!client.Login(nick, user, password))
    {
        std::osyncstream(std::cerr) << "Failed to log in." << std::endl;
        if (client.Connected())
            client.Disconnect();
        return 1;
    }

    std::osyncstream(std::cout) << "Logged." << std::endl;

    ConsoleInput console;
    if (!console.valid())
    {
        std::osyncstream(std::cerr) << "Failed to initialize console input." << std::endl;
        if (client.Connected())
            client.Disconnect();
        return 1;
    }

    commandHandler.AddCommand("msg", 2, &msgCommand);
    commandHandler.AddCommand("join", 1, &joinCommand);
    commandHandler.AddCommand("part", 1, &partCommand);
    commandHandler.AddCommand("ctcp", 2, &ctcpCommand);

    ShutdownCoordinator shutdown;
    ConsoleInterrupt interrupt([&shutdown, &console] {
        shutdown.request();
        console.request_stop();
    });

    std::jthread receive_worker;
    std::jthread input_worker;

    try
    {
        receive_worker = std::jthread([&client, &console, &shutdown] {
            while (client.Connected() && !shutdown.requested())
                client.ReceiveData();
            shutdown.request();
            console.request_stop();
        });
    }
    catch (std::system_error const&)
    {
        if (client.Connected())
            client.Disconnect();
        std::osyncstream(std::cerr) << "Failed to start receive thread." << std::endl;
        return 1;
    }

    try
    {
        input_worker = std::jthread([&client, &console, &shutdown](std::stop_token stop) {
            std::stop_callback wake_on_stop(stop, [&console] { console.request_stop(); });

            while (!stop.stop_requested() && !console.stop_requested() && !shutdown.requested())
            {
                ConsoleInput::ReadResult const input = console.read_line();
                if (input.status == ConsoleInput::Status::Stopped
                    || input.status == ConsoleInput::Status::Error)
                {
                    shutdown.request();
                    break;
                }
                if (input.status == ConsoleInput::Status::Eof)
                {
                    shutdown.request();
                    if (client.Connected())
                        client.SendIRC("QUIT");
                    break;
                }
                if (input.line.empty())
                    continue;

                if (IsQuitCommand(input.line))
                {
                    shutdown.request();
                    if (client.Connected())
                        client.SendIRC("QUIT");
                    break;
                }

                if (input.line.front() == '/')
                    commandHandler.ParseCommand(input.line, &client);
                else
                    client.SendIRC(input.line);
            }

            shutdown.request();
        });
    }
    catch (std::system_error const&)
    {
        shutdown.request();
        console.request_stop();
        if (client.Connected())
            client.Disconnect();
        std::osyncstream(std::cerr) << "Failed to start input thread." << std::endl;
        return 1;
    }

    shutdown.wait();

    console.request_stop();
    if (input_worker.joinable())
        input_worker.request_stop();
    if (receive_worker.joinable())
        receive_worker.request_stop();

    if (client.Connected())
        client.Disconnect();

    if (input_worker.joinable())
        input_worker.join();
    if (receive_worker.joinable())
        receive_worker.join();

    std::osyncstream(std::cout) << "Disconnected." << std::endl;
    return 0;
}
