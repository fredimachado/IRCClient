## C++ Console IRC Client
Copyright (C) Fredi Machado (https://github.com/Fredi)

####GNU GPL
> IRCClient is free software; you can redistribute it and/or modify
> it under the terms of the GNU General Public License as published by
> the Free Software Foundation; either version 2 of the License, or
> (at your option) any later version.
>
> This program is distributed in the hope that it will be useful,
> but WITHOUT ANY WARRANTY; without even the implied warranty of
> MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
> GNU General Public License for more details.
>
> You should have received a copy of the GNU General Public License
> along with this program; if not, write to the Free Software
> Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

###Simple console IRC Client written in C++
- Can be used as an IRC bot
- It has a simple hook system where you can do whatever you want  when
  receiving an IRC command.
- Example in Main.cpp

####Hooking IRC commands:
First create a function with this two arguments:

```cpp
int onPrivMsg(IRCMessage message, IRCClient* client)
{
    // Check who sent the message
    if (message.prefix.nick != "YourNick")
        return 1;
    
    // received text
    std::string text = message.parameters.at(message.parameters.size() - 1);
    
    if (text == "join #channel")
        client->SendIRC("JOIN #channel");
    if (text == "leave #channel")
        client->SendIRC("PART #channel");
    if (text == "quit now")
        client->SendIRC("QUIT");
    
    return 0;
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
