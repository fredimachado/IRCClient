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

#include "IRCClient.h"
#include "IRCHandler.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    char ToUpperAscii(char ch)
    {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    void ToUpperAsciiInPlace(std::string& text)
    {
        std::transform(text.begin(), text.end(), text.begin(), ToUpperAscii);
    }

    bool ContainsIrcForbidden(std::string_view text)
    {
        return text.find('\r') != std::string_view::npos
            || text.find('\n') != std::string_view::npos
            || text.find('\0') != std::string_view::npos;
    }

    bool IsValidRegistrationField(std::string_view field)
    {
        if (field.empty() || ContainsIrcForbidden(field))
            return false;
        return field.find(' ') == std::string_view::npos;
    }

    bool IsValidIrcCommand(std::string_view command)
    {
        if (command.empty())
            return false;

        if (command.size() == 3
            && std::isdigit(static_cast<unsigned char>(command[0]))
            && std::isdigit(static_cast<unsigned char>(command[1]))
            && std::isdigit(static_cast<unsigned char>(command[2])))
        {
            return true;
        }

        return std::all_of(command.begin(), command.end(), [](char ch) {
            return std::isalpha(static_cast<unsigned char>(ch)) != 0;
        });
    }

    std::vector<std::string> ParseIrcParams(std::string const& data)
    {
        std::vector<std::string> parameters;
        if (data.empty())
            return parameters;

        std::size_t pos = 0;
        while (pos < data.size() && parameters.size() < 14)
        {
            if (data[pos] == ':')
            {
                parameters.push_back(data.substr(pos + 1));
                return parameters;
            }

            auto const space = data.find(' ', pos);
            if (space == std::string::npos)
            {
                parameters.push_back(data.substr(pos));
                return parameters;
            }

            if (space > pos)
                parameters.push_back(data.substr(pos, space - pos));
            pos = space + 1;
        }

        if (pos < data.size() && parameters.size() == 14)
        {
            if (data[pos] == ':')
                parameters.push_back(data.substr(pos + 1));
            else
                parameters.push_back(data.substr(pos));
        }

        return parameters;
    }

    bool FitsIrcFrame(std::string_view line)
    {
        return line.size() + 2 <= IRCClient::kMaxIrcFrameBytes;
    }
}

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

bool IRCCommandPrefix::Parse(std::string data)
{
    prefix.clear();
    nick.clear();
    user.clear();
    host.clear();

    if (data.empty() || data.front() != ':')
        return false;

    auto const space = data.find(' ');
    if (space == std::string::npos || space <= 1)
        return false;

    prefix = data.substr(1, space - 1);
    if (prefix.empty() || ContainsIrcForbidden(prefix))
        return false;

    auto const at = prefix.find('@');
    std::string nickpart = prefix;
    if (at != std::string::npos)
    {
        if (at == 0 || at + 1 == prefix.size())
            return false;
        nickpart = prefix.substr(0, at);
        host = prefix.substr(at + 1);
        if (host.empty() || host.find('@') != std::string::npos)
            return false;
    }

    auto const bang = nickpart.find('!');
    if (bang != std::string::npos)
    {
        if (bang == 0 || bang + 1 == nickpart.size())
            return false;
        nick = nickpart.substr(0, bang);
        user = nickpart.substr(bang + 1);
        if (user.empty() || user.find('!') != std::string::npos)
            return false;
    }
    else if (at != std::string::npos)
    {
        nick = std::move(nickpart);
        if (nick.empty())
            return false;
    }

    return true;
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
    if (data.empty() || ContainsIrcForbidden(data) || !FitsIrcFrame(data))
        return false;

    data.append("\r\n");
    std::lock_guard<std::mutex> lock(_send_mutex);
    return _socket.SendData(data.c_str());
}

bool IRCClient::TrySendIRC(std::string data)
{
    if (data.empty() || ContainsIrcForbidden(data) || !FitsIrcFrame(data))
        return false;

    data.append("\r\n");
    std::unique_lock<std::mutex> lock(_send_mutex, std::try_to_lock);
    if (!lock.owns_lock())
        return false;
    return _socket.SendData(data.c_str());
}

bool IRCClient::Login(std::string nick, std::string user, std::string password)
{
    if (!IsValidRegistrationField(nick) || !IsValidRegistrationField(user))
        return false;
    if (!password.empty() && !IsValidRegistrationField(password))
        return false;

    std::string const nick_line = "NICK " + nick;
    std::string const user_line = "USER " + user + " 8 * :Cpp IRC Client";
    if (!FitsIrcFrame(nick_line) || !FitsIrcFrame(user_line))
        return false;

    std::string payload;
    if (!password.empty())
    {
        std::string const pass_line = "PASS " + password;
        if (!FitsIrcFrame(pass_line))
            return false;
        payload += pass_line;
        payload += "\r\n";
    }
    payload += nick_line;
    payload += "\r\n";
    payload += user_line;
    payload += "\r\n";

    _nick = std::move(nick);
    _user = std::move(user);

    std::lock_guard<std::mutex> lock(_send_mutex);
    return _socket.SendData(payload.c_str());
}

void IRCClient::ReceiveData()
{
    std::string const chunk = _socket.ReceiveData();
    _inbound.append(chunk.data(), chunk.size());
    ExtractFrames();
}

void IRCClient::ExtractFrames()
{
    for (;;)
    {
        if (_discarding_overlong)
        {
            auto const delim = _inbound.find("\r\n");
            if (delim == std::string::npos)
            {
                if (_inbound.empty())
                    return;
                char const last = _inbound.back();
                _inbound.clear();
                if (last == '\r')
                    _inbound.push_back('\r');
                return;
            }

            _inbound.erase(0, delim + 2);
            _discarding_overlong = false;
            continue;
        }

        auto const delim = _inbound.find("\r\n");
        if (delim == std::string::npos)
        {
            if (_inbound.size() >= kMaxIrcFrameBytes)
            {
                _discarding_overlong = true;
                char const last = _inbound.back();
                _inbound.clear();
                if (last == '\r')
                    _inbound.push_back('\r');
                continue;
            }
            return;
        }

        auto const frame_bytes = delim + 2;
        if (frame_bytes > kMaxIrcFrameBytes)
        {
            _inbound.erase(0, frame_bytes);
            continue;
        }

        std::string frame = _inbound.substr(0, delim);
        _inbound.erase(0, frame_bytes);

        if (frame.find('\0') != std::string::npos)
            continue;

        Parse(std::move(frame));
    }
}

void IRCClient::Parse(std::string data)
{
    if (data.empty() || ContainsIrcForbidden(data))
        return;

    // IRCv3 message tags (@tag=value;...) are unsupported.
    if (data.front() == '@')
        return;

    std::string const original(data);
    IRCCommandPrefix cmdPrefix;

    if (data.front() == ':')
    {
        if (!cmdPrefix.Parse(data))
            return;

        auto const space = data.find(' ');
        if (space == std::string::npos || space + 1 >= data.size())
            return;
        data = data.substr(space + 1);
        while (!data.empty() && data.front() == ' ')
            data.erase(0, 1);
        if (data.empty())
            return;
    }

    auto const cmd_end = data.find(' ');
    std::string command = (cmd_end == std::string::npos) ? data : data.substr(0, cmd_end);
    if (!IsValidIrcCommand(command))
        return;
    ToUpperAsciiInPlace(command);

    if (cmd_end == std::string::npos)
        data.clear();
    else
        data = data.substr(cmd_end + 1);

    std::vector<std::string> const parameters = ParseIrcParams(data);
    IRCMessage ircMessage(command, cmdPrefix, parameters);

    if (command == "ERROR")
    {
        std::cout << original << std::endl;
        Disconnect();
        CallHook(command, ircMessage);
        return;
    }

    if (command == "PING")
    {
        std::cout << "Ping? Pong!" << std::endl;
        if (!parameters.empty())
            SendIRC("PONG :" + parameters.front());
        CallHook(command, ircMessage);
        return;
    }

    int const commandIndex = GetCommandHandler(command);
    if (commandIndex < NUM_IRC_CMDS)
    {
        IRCCommandHandler& cmdHandler = ircCommandTable[commandIndex];
        (this->*cmdHandler.handler)(ircMessage);
    }
    else if (_debug)
        std::cout << original << std::endl;

    CallHook(command, ircMessage);
}

void IRCClient::HookIRCCommand(std::string command, void (*function)(IRCMessage /*message*/, IRCClient* /*client*/))
{
    if (function == nullptr || command.empty())
        return;

    ToUpperAsciiInPlace(command);

    IRCCommandHook hook;
    hook.command = std::move(command);
    hook.function = function;
    _hooks.push_back(hook);
}

void IRCClient::CallHook(std::string command, IRCMessage message)
{
    if (_hooks.empty())
        return;

    ToUpperAsciiInPlace(command);

    for (std::list<IRCCommandHook>::const_iterator itr = _hooks.begin(); itr != _hooks.end(); ++itr)
    {
        if (itr->function != nullptr && itr->command == command)
        {
            (*(itr->function))(message, this);
            break;
        }
    }
}
