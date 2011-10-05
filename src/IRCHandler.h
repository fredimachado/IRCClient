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

inline int const GetCommandHandler(std::string command)
{
    for (int i = 0; i < NUM_IRC_CMDS; ++i)
    {
        if (ircCommandTable[i].command == command)
            return i;
    }

    return NUM_IRC_CMDS;
}

#endif
