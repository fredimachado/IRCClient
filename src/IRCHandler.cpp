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

#include "IRCHandler.h"

#include <iostream>
#include <string>
#include <vector>

IRCCommandHandler ircCommandTable[NUM_IRC_CMDS] =
{
    { "PRIVMSG",            &IRCClient::HandlePrivMsg                   },
    { "NOTICE",             &IRCClient::HandleNotice                    },
    { "JOIN",               &IRCClient::HandleChannelJoinPart           },
    { "PART",               &IRCClient::HandleChannelJoinPart           },
    { "NICK",               &IRCClient::HandleUserNickChange            },
    { "QUIT",               &IRCClient::HandleUserQuit                  },
    { "353",                &IRCClient::HandleChannelNamesList          },
    { "433",                &IRCClient::HandleNicknameInUse             },
    { "001",                &IRCClient::HandleServerMessage             },
    { "002",                &IRCClient::HandleServerMessage             },
    { "003",                &IRCClient::HandleServerMessage             },
    { "004",                &IRCClient::HandleServerMessage             },
    { "005",                &IRCClient::HandleServerMessage             },
    { "250",                &IRCClient::HandleServerMessage             },
    { "251",                &IRCClient::HandleServerMessage             },
    { "252",                &IRCClient::HandleServerMessage             },
    { "253",                &IRCClient::HandleServerMessage             },
    { "254",                &IRCClient::HandleServerMessage             },
    { "255",                &IRCClient::HandleServerMessage             },
    { "265",                &IRCClient::HandleServerMessage             },
    { "266",                &IRCClient::HandleServerMessage             },
    { "366",                &IRCClient::HandleServerMessage             },
    { "372",                &IRCClient::HandleServerMessage             },
    { "375",                &IRCClient::HandleServerMessage             },
    { "376",                &IRCClient::HandleServerMessage             },
    { "439",                &IRCClient::HandleServerMessage             },
};

namespace
{
    bool HasCtcpDelimiters(std::string const& text)
    {
        return text.size() >= 2 && text.front() == '\001' && text.back() == '\001';
    }
}

void IRCClient::HandleCTCP(IRCMessage message)
{
    if (message.parameters.empty())
        return;

    std::string const& to = message.parameters.front();
    std::string text = message.parameters.back();
    if (!HasCtcpDelimiters(text))
        return;

    text = text.substr(1, text.size() - 2);

    std::cout << "[" + message.prefix.nick << " requested CTCP " << text << "]" << std::endl;

    if (to == _nick)
    {
        if (text == "VERSION")
        {
            SendIRC("NOTICE " + message.prefix.nick + " :\001VERSION Open source IRC client by Fredi Machado - https://github.com/fredimachado/IRCClient \001");
            return;
        }

        SendIRC("NOTICE " + message.prefix.nick + " :\001ERRMSG " + text + " :Not implemented\001");
    }
}

void IRCClient::HandlePrivMsg(IRCMessage message)
{
    if (message.parameters.empty())
        return;

    std::string const& to = message.parameters.front();
    std::string const& text = message.parameters.back();

    if (!text.empty() && text.front() == '\001')
    {
        if (HasCtcpDelimiters(text))
            HandleCTCP(message);
        return;
    }

    if (!to.empty() && to.front() == '#')
        std::cout << "From " + message.prefix.nick << " @ " + to + ": " << text << std::endl;
    else
        std::cout << "From " + message.prefix.nick << ": " << text << std::endl;
}

void IRCClient::HandleNotice(IRCMessage message)
{
    std::string from = message.prefix.nick != "" ? message.prefix.nick : message.prefix.prefix;
    std::string text;

    if (!message.parameters.empty())
        text = message.parameters.back();

    if (!text.empty() && text.front() == '\001')
    {
        if (!HasCtcpDelimiters(text))
            return;

        text = text.substr(1, text.size() - 2);
        if (text.find(" ") == std::string::npos)
        {
            std::cout << "[Invalid " << text << " reply from " << from << "]" << std::endl;
            return;
        }
        std::string ctcp = text.substr(0, text.find(" "));
        std::cout << "[" << from << " " << ctcp << " reply]: " << text.substr(text.find(" ") + 1) << std::endl;
    }
    else
        std::cout << "-" << from << "- " << text << std::endl;
}

void IRCClient::HandleChannelJoinPart(IRCMessage message)
{
    if (message.parameters.empty())
        return;

    std::string const& channel = message.parameters.front();
    std::string action = message.command == "JOIN" ? "joins" : "leaves";
    std::cout << message.prefix.nick << " " << action << " " << channel << std::endl;
}

void IRCClient::HandleUserNickChange(IRCMessage message)
{
    if (message.parameters.empty())
        return;

    std::string const& newNick = message.parameters.front();
    std::cout << message.prefix.nick << " changed his nick to " << newNick << std::endl;
}

void IRCClient::HandleUserQuit(IRCMessage message)
{
    std::string text;
    if (!message.parameters.empty())
        text = message.parameters.front();
    std::cout << message.prefix.nick << " quits (" << text << ")" << std::endl;
}

void IRCClient::HandleChannelNamesList(IRCMessage message)
{
    if (message.parameters.size() < 4)
        return;

    std::string const& channel = message.parameters.at(2);
    std::string const& nicks = message.parameters.at(3);
    std::cout << "People on " << channel << ":" << std::endl << nicks << std::endl;
}

void IRCClient::HandleNicknameInUse(IRCMessage message)
{
    if (message.parameters.size() < 3)
        return;

    std::cout << message.parameters.at(1) << " " << message.parameters.at(2) << std::endl;
}

void IRCClient::HandleServerMessage(IRCMessage message)
{
    if (message.parameters.empty())
        return;

    std::vector<std::string>::const_iterator itr = message.parameters.begin();
    ++itr;
    for (; itr != message.parameters.end(); ++itr)
        std::cout << *itr << " ";
    std::cout << std::endl;
}
