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

#ifndef _IRCHANDLER_H
#define _IRCHANDLER_H

#include "IRCClient.h"

#define NUM_IRC_CMDS 26

struct IRCCommandHandler
{
    std::string command;
    void (IRCClient::*handler)(IRCMessage /*message*/);
};

extern IRCCommandHandler ircCommandTable[NUM_IRC_CMDS];

inline int GetCommandHandler(std::string command)
{
    for (int i = 0; i < NUM_IRC_CMDS; ++i)
    {
        if (ircCommandTable[i].command == command)
            return i;
    }

    return NUM_IRC_CMDS;
}

#endif
