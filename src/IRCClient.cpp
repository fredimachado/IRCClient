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
#include <algorithm>
#include "IRCSocket.h"
#include "IRCClient.h"

std::vector<std::string> split(std::string const& text, char sep)
{
    std::vector<std::string> tokens;
    size_t start = 0, end = 0;
    while ((end = text.find(sep, start)) != std::string::npos)
    {
        tokens.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    tokens.push_back(text.substr(start));
    return tokens;
}

bool IRCClient::InitSocket()
{
    return _socket.Init();
}

bool IRCClient::Connect(char* host, int port)
{
    return _socket.Connect(host, port);
}

void IRCClient::Disconnect()
{
    _socket.Disconnect();
}

bool IRCClient::SendIRC(std::string data)
{
    data.append("\n");
    return _socket.SendData(data.c_str());
}

bool IRCClient::Login(std::string nick, std::string user)
{
    _nick = nick;
    _user = user;

    if (SendIRC("HELLO"))
        if (SendIRC("NICK " + nick))
            if (SendIRC("USER " + user + " 8 * :Cpp IRC Client"))
                return true;

    return false;
}

void IRCClient::ReceiveData()
{
    std::string buffer = _socket.ReceiveData();

    std::string line;
    std::istringstream iss(buffer);
    while(getline(iss, line))
    {
        if (line.find("\r") != std::string::npos)
            line = line.substr(0, line.size() - 1);
        Parse(line);
    }
}

void IRCClient::HandleCommand(IRCMessage message)
{
    if (message.command == "PRIVMSG" || message.command == "NOTICE")
    {
        std::string to = message.parameters.at(0);
        std::string text = message.parameters.at(message.parameters.size() - 1);

        // Message to us
        if (to == _nick)
            if (text == "\001VERSION\001") // Respond to CTCP VERSION
                SendIRC("PRIVMSG " + message.prefix.nick + " :\001VERSION IRCClient by Fredi Machado - https://github.com/Fredi/IRCClient \001");
    }
}

void IRCClient::Parse(std::string data)
{
    std::string original(data);
    IRCCommandPrefix cmdPrefix;

    // if command has prefix
    if (data.substr(0, 1) == ":")
    {
        cmdPrefix.Parse(data);
        data = data.substr(data.find(" ") + 1);
    }

    std::string command = data.substr(0, data.find(" "));
    std::transform(command.begin(), command.end(), command.begin(), towupper);
    if (data.find(" ") != std::string::npos)
        data = data.substr(data.find(" ") + 1);
    else
        data = "";

    std::vector<std::string> parameters;

    if (data != "")
    {
        if (data.substr(0, 1) == ":")
            parameters.push_back(data.substr(1));
        else
        {
            size_t pos1 = 0, pos2;
            while ((pos2 = data.find(" ", pos1)) != std::string::npos)
            {
                parameters.push_back(data.substr(pos1, pos2 - pos1));
                pos1 = pos2 + 1;
                if (data.substr(pos1, 1) == ":")
                {
                    parameters.push_back(data.substr(pos1 + 1));
                    break;
                }
            }
            if (parameters.empty())
                parameters.push_back(data);
        }
    }

    if (command == "ERROR")
    {
        Disconnect();
        return;
    }

    if (command == "PING")
        SendIRC("PONG :" + parameters.at(0));

    IRCMessage ircMessage(command, cmdPrefix, parameters);

    // Default handler
    HandleCommand(ircMessage);

    // Try to call hook (if any matches)
    CallHook(command, ircMessage);

    std::cout << original << std::endl;
}

void IRCClient::HookIRCCommand(std::string command, int (*function)(IRCMessage /*message*/, void*))
{
    IRCCommandHook hook;

    hook.command = command;
    hook.function = function;

    _hooks.push_back(hook);
}

void IRCClient::CallHook(std::string command, IRCMessage message)
{
    if (_hooks.empty())
        return;

    for (std::list<IRCCommandHook>::const_iterator itr = _hooks.begin(); itr != _hooks.end(); ++itr)
    {
        if (itr->command == command)
        {
            (*(itr->function))(message, this);
            break;
        }
    }
}
