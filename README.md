## Simple cross-platform Console IRC Client

[![Build](https://github.com/fredimachado/IRCClient/actions/workflows/ci.yml/badge.svg)](https://github.com/fredimachado/IRCClient/actions/workflows/ci.yml)

- It works on Windows, Linux, and macOS
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

### Building

CMake 3.20 or newer and a C++20 compiler. From the repository root:

```sh
cmake --preset default && cmake --build --preset default && ctest --preset default
```

The executable is `ircclient` on Unix and `ircclient.exe` on Windows:

```sh
ircclient host port [nick] [user] [password]
```

`nick` defaults to `MyIRCClient`, `user` defaults to `IRCClient`, and `password` is sent only when you pass it.

### Sanitizers and coverage (Linux)

These presets need Ninja. Do not combine them.

AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
cmake --preset linux-sanitizer && cmake --build --preset linux-sanitizer && ctest --preset linux-sanitizer
```

Coverage instrumentation for `src/` (CI compares gcovr line and branch percentages against `cmake/coverage-baseline.json`):

```sh
cmake --preset linux-coverage && cmake --build --preset linux-coverage && ctest --preset linux-coverage
```

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
