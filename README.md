Copyright (C) Fredi Machado (https://github.com/Fredi)

  IRCClient is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


Simple console IRC Client writen in C++

- Currently can be used as an IRC bot, since doesn't accept input yet.
- It has a simple hook system where you can do whatever you want  when receiving an IRC command.
- Example in Main.cpp

BUILDING ON LINUX:

- Edit Makefile and remove -lws2_32 from LDFLAGS (on line 3).
  I need to find a better way (suggestions?)
- You could also remove ".exe" from EXECUTABLE
