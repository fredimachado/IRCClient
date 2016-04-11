## C++ Console IRC Client
Copyright (c) 2011-2016, Fredi Machado (https://github.com/Fredi), All rights reserved.

Windows | Linux
:------------: | :------------:
[![Windows Build status](https://ci.appveyor.com/api/projects/status/pn55ra5fr2c1b6t7?svg=true)](https://ci.appveyor.com/project/Fredi/ircclient) | [![Linux Build Status](https://travis-ci.org/Fredi/IRCClient.svg?branch=master)](https://travis-ci.org/Fredi/IRCClient)

####GNU LGPL
> IRCClient is free software; you can redistribute it and/or
> modify it under the terms of the GNU Lesser General Public
> License as published by the Free Software Foundation; either
> version 3.0 of the License, or any later version.
>
> This program is distributed in the hope that it will be useful,
> but WITHOUT ANY WARRANTY; without even the implied warranty of
> MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
> Lesser General Public License for more details.
>
> http://www.gnu.org/licenses/lgpl.html

###Simple console IRC Client written in C++
- It works on windows and linux
- Can be used as an IRC bot
- It has a simple hook system where you can do whatever you want  when
  receiving an IRC command.
- Example in Main.cpp

####Hooking IRC commands:
First create a function (name it whatever you want) with two arguments, an IRCMessage and a pointer to IRCClient:

```cpp
void onPrivMsg(IRCMessage message, IRCClient* client)
{
    // Check who can "control" us
    if (message.prefix.nick != "YourNick")
        return;
    
    // received text
    std::string text = message.parameters.at(message.parameters.size() - 1);
    
    if (text == "join #channel")
        client->SendIRC("JOIN #channel");
    if (text == "leave #channel")
        client->SendIRC("PART #channel");
    if (text == "quit now")
        client->SendIRC("QUIT");
}
```

Then, after you create the IRCClient instance, you can hook it:

```cpp
IRCClient client;

// Hook PRIVMSG
client.HookIRCCommand("PRIVMSG", &onPrivMsg);
```


####Building on windows with Mingw:

- Edit Makefile
- Replace "-lpthread" with "-lws2_32" (no quotes) in LDFLAGS on line 3.
- Add ".exe" extension (no quotes) to the EXECUTABLE filename (line 8).
