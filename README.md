## Simple cross-platform Console IRC Client

[![Build](https://github.com/fredimachado/IRCClient/actions/workflows/ci.yml/badge.svg)](https://github.com/fredimachado/IRCClient/actions/workflows/ci.yml)

- It works on windows and linux (haven't tested on mac)
- Can be used as an IRC bot
- It has a simple hook system where you can do whatever you want  when
  receiving an IRC command.
- Example in Main.cpp

### Hooking IRC commands:
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

### Building on Windows with MinGW

Install MSYS2 from PowerShell:

```powershell
winget install --id MSYS2.MSYS2 --exact
```

Open an MSYS2 UCRT64 terminal and install GCC and Make:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
```

From the repository directory, build the client:

```sh
make
```

The executable is written to `ircclient.exe`. The Makefile adds the required
Windows socket library automatically.

## Contribution
Just send a pull request! :)

## GNU LGPL
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
