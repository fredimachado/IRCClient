/**
 * Thread
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
