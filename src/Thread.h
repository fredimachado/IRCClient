/**
 * Thread
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
