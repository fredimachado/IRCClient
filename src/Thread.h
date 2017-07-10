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

#ifndef _THREAD_H
#define _THREAD_H

#ifdef _WIN32
#include <process.h>
typedef void (*ThreadFunction)(void* /*param*/);
typedef void ThreadReturn;
typedef int ThreadId;
#else
#include "pthread.h"
typedef void* (*ThreadFunction)(void* /*param*/);
typedef void* ThreadReturn;
typedef pthread_t ThreadId;
#endif

class Thread
{
private:
    ThreadId _threadId;

public:
    Thread();
    ~Thread();

    bool Start(ThreadFunction /*callback*/, void* /*param*/);
};

#endif
