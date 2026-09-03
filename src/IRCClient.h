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

#ifndef _IRCCLIENT_H
#define _IRCCLIENT_H

#include "IRCSocket.h"
#include "SocketOps.h"

#include <cstddef>
#include <list>
#include <mutex>
#include <string>
#include <vector>

class IRCClient;

extern std::vector<std::string> split(std::string const&, char);

struct IRCCommandPrefix
{
    // Returns false and clears all fields when the prefix is missing or
    // malformed. Do not read nick, user, or host after a failed parse.
    bool Parse(std::string data);

    std::string prefix;
    std::string nick;
    std::string user;
    std::string host;
};

struct IRCMessage
{
    IRCMessage() = default;
    IRCMessage(std::string cmd, IRCCommandPrefix p, std::vector<std::string> params) :
        command(cmd), prefix(p), parameters(params) {};

    std::string command;
    IRCCommandPrefix prefix;
    std::vector<std::string> parameters;
};

struct IRCCommandHook
{
    IRCCommandHook() : function(NULL) {};

    std::string command;
    void (*function)(IRCMessage /*message*/, IRCClient* /*client*/);
};

class IRCClient
{
public:
    // RFC 1459 section 2.3: a message shall not exceed 512 bytes, counting
    // every character including the terminating CR-LF. This client uses that
    // limit for inbound reassembly and for each outbound SendIRC line.
    // IRCv3 message tags and CAP negotiation are unsupported; tagged lines
    // and CAP traffic are ignored.
    static constexpr std::size_t kMaxIrcFrameBytes = 512;

    IRCClient() : _debug(false) {};
    explicit IRCClient(SocketOps& ops) : _socket(ops), _debug(false) {};

    bool InitSocket();
    bool Connect(char* /*host*/, int /*port*/);
    void Disconnect();
    bool Connected() { return _socket.Connected(); };

    bool SendIRC(std::string /*data*/);
    bool TrySendIRC(std::string /*data*/);

    // Registers with an optional PASS, then NICK, then USER. The three
    // lines are sent in one write so other SendIRC callers cannot interleave
    // them. USER keeps the form `USER <user> 8 * :Cpp IRC Client`
    // (RFC 2812 mode 8 = invisible).
    bool Login(std::string /*nick*/, std::string /*user*/, std::string /*password*/ = std::string());

    void ReceiveData();

    void HookIRCCommand(std::string /*command*/, void (*function)(IRCMessage /*message*/, IRCClient* /*client*/));

    void Parse(std::string /*data*/);

    void HandleCTCP(IRCMessage /*message*/);

    // Default internal handlers
    void HandlePrivMsg(IRCMessage /*message*/);
    void HandleNotice(IRCMessage /*message*/);
    void HandleChannelJoinPart(IRCMessage /*message*/);
    void HandleUserNickChange(IRCMessage /*message*/);
    void HandleUserQuit(IRCMessage /*message*/);
    void HandleChannelNamesList(IRCMessage /*message*/);
    void HandleNicknameInUse(IRCMessage /*message*/);
    void HandleServerMessage(IRCMessage /*message*/);

    void Debug(bool debug) { _debug = debug; };

private:
    void HandleCommand(IRCMessage /*message*/);
    void CallHook(std::string /*command*/, IRCMessage /*message*/);
    void ExtractFrames();

    IRCSocket _socket;
    std::mutex _send_mutex;

    std::list<IRCCommandHook> _hooks;

    std::string _inbound;
    bool _discarding_overlong = false;

    std::string _nick;
    std::string _user;

    bool _debug;
};

#endif
