/**
 * IRC Client
 *
 * Fredi Machado
 * http://github.com/Fredi
 */

#ifndef _IRCCLIENT_H
#define _IRCCLIENT_H

#include <string>
#include <vector>
#include "IRCSocket.h"

extern void split(std::vector<std::string>&, std::string const&, char);

struct IRCCommandPrefix
{
    void Parse(std::string data)
    {
        if (data == "")
            return;

        prefix = data.substr(1, data.find(" "));
        std::vector<std::string> tokens;

        if (prefix.find("@") != std::string::npos)
        {
            split(tokens, prefix, '@');
            nick = tokens.at(0);
            host = tokens.at(1);
        }
        if (nick != "" && nick.find("!") != std::string::npos)
        {
            split(tokens, nick, '!');
            nick = tokens.at(0);
            user = tokens.at(1);
        }
    };

    std::string prefix;
    std::string nick;
    std::string user;
    std::string host;
};

struct IRCCommandHook
{
    IRCCommandHook() : function(NULL), next(NULL) {};

    std::string command;
    int (*function)(IRCCommandPrefix /*prefix*/, std::vector<std::string> /*parameters*/, void*);
    IRCCommandHook* next;
};

class IRCClient
{
public:
    IRCClient() : _hooks(NULL) {};
    ~IRCClient();

    bool InitSocket();
    bool Connect(char* /*host*/, int /*port*/);
    void Disconnect();
    bool Connected() { return _socket.Connected(); };

    bool SendIRC(std::string /*data*/);

    bool Login(std::string /*nick*/, std::string /*user*/);

    void ReceiveData();

    void HookIRCCommand(std::string /*command*/, int (*function)(IRCCommandPrefix /*prefix*/, std::vector<std::string> /*parameters*/, void*));

    void Parse(std::string /*data*/);

private:
    void CallHook(std::string /*command*/, IRCCommandPrefix /*prefix*/, std::vector<std::string> /*parameters*/);
    void AddIRCCommandHook(IRCCommandHook* /*hook*/, std::string /*command*/, int (*function)(IRCCommandPrefix /*prefix*/, std::vector<std::string> /*parameters*/, void*));
	void DeleteIRCCommandHook(IRCCommandHook* /*hook*/);

    IRCSocket _socket;

    IRCCommandHook* _hooks;
};

#endif
