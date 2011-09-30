/**
 * IRC Client
 *
 * Fredi Machado
 * http://github.com/Fredi
 */

#include <iostream>
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

IRCClient::~IRCClient()
{
    if (_hooks)
        DeleteIRCCommandHook(_hooks);
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
    if (SendIRC("HELLO"))
        if (SendIRC("NICK " + nick))
            if (SendIRC("USER " + user + " 0 * :Cpp IRC Client"))
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

    CallHook(command, cmdPrefix, parameters);

    std::cout << original << std::endl;
}

void IRCClient::AddIRCCommandHook(IRCCommandHook* hook, std::string command, int (*function)(IRCCommandPrefix /*prefix*/, std::vector<std::string> /*parameters*/, void*))
{
    if (hook->function)
    {
        if (!hook->next)
            hook->next = new IRCCommandHook();
        AddIRCCommandHook(hook->next, command, function);
    }
    else
    {
        hook->function = function;
        hook->command = command;
    }
}

void IRCClient::DeleteIRCCommandHook(IRCCommandHook* hook)
{
    if (hook->next)
        DeleteIRCCommandHook(hook->next);

    delete hook;
}

void IRCClient::HookIRCCommand(std::string command, int (*function)(IRCCommandPrefix /*prefix*/, std::vector<std::string> /*parameters*/, void*))
{
    if (!_hooks)
        _hooks = new IRCCommandHook();

    AddIRCCommandHook(_hooks, command, function);
}

void IRCClient::CallHook(std::string command, IRCCommandPrefix prefix, std::vector<std::string> parameters)
{
    if (!_hooks)
        return;

    IRCCommandHook* hook = _hooks;

    while (hook)
    {
        if (hook->command == command)
        {
            (*(hook->function))(prefix, parameters, this);
            hook = NULL;
        }
        else
            hook = hook->next;
    }
}
