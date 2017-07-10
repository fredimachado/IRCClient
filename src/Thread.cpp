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

#include "Thread.h"

Thread::Thread() : _threadId(0) {}

Thread::~Thread()
{
#ifndef _WIN32
    pthread_exit(NULL);
#endif
}

bool Thread::Start(ThreadFunction callback, void* param)
{
#ifdef _WIN32
    _threadId = _beginthread(callback, 0, param);
    if (_threadId)
        return true;
#else
    if (pthread_create(&_threadId, NULL, *callback, param) == 0)
        return true;
#endif

    return false;
}
