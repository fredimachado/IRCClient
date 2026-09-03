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

#include "ConsoleInterrupt.h"

#include <atomic>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#endif

namespace
{
std::atomic<ConsoleInterrupt*> g_active{nullptr};
std::mutex g_handler_mutex;

void clear_active(ConsoleInterrupt* instance)
{
    ConsoleInterrupt* expected = instance;
    g_active.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

#ifdef _WIN32
BOOL WINAPI OnConsoleCtrl(DWORD type)
{
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT)
        return FALSE;

    std::lock_guard<std::mutex> lock(g_handler_mutex);
    if (ConsoleInterrupt* active = g_active.load(std::memory_order_acquire))
        active->request();
    return TRUE;
}
#endif
}

struct ConsoleInterrupt::Impl
{
    std::function<void()> on_interrupt;
    std::atomic<bool> interrupted{false};
    std::atomic<bool> ready{false};

#ifdef _WIN32
    bool handler_installed = false;
    bool attached_parent_console = false;
#else
    sigset_t wait_set{};
    sigset_t previous_mask{};
    bool mask_changed = false;
    std::atomic<bool> stop_waiter{false};
    std::atomic<bool> tid_ready{false};
    pthread_t waiter_tid{};
    std::thread waiter;
#endif

    explicit Impl(std::function<void()> callback)
        : on_interrupt(std::move(callback))
    {
    }

    void fire()
    {
        if (interrupted.exchange(true, std::memory_order_acq_rel))
            return;
        if (on_interrupt)
            on_interrupt();
    }

#ifndef _WIN32
    void run_waiter()
    {
        waiter_tid = pthread_self();
        tid_ready.store(true, std::memory_order_release);

        int sig = 0;
        while (!stop_waiter.load(std::memory_order_acquire))
        {
            int const rc = sigwait(&wait_set, &sig);
            if (stop_waiter.load(std::memory_order_acquire))
                break;
            if (rc == 0)
                fire();
        }
    }

    void stop_and_join_waiter()
    {
        stop_waiter.store(true, std::memory_order_release);
        if (!waiter.joinable())
            return;

        while (!tid_ready.load(std::memory_order_acquire))
            std::this_thread::yield();
        pthread_kill(waiter_tid, SIGINT);
        waiter.join();
    }
#endif

    ~Impl()
    {
#ifdef _WIN32
        if (handler_installed)
            SetConsoleCtrlHandler(&OnConsoleCtrl, FALSE);
        if (attached_parent_console)
            FreeConsole();
#else
        stop_and_join_waiter();
        if (mask_changed)
            pthread_sigmask(SIG_SETMASK, &previous_mask, nullptr);
#endif
    }
};

ConsoleInterrupt::ConsoleInterrupt(std::function<void()> on_interrupt)
    : impl_(std::make_unique<Impl>(std::move(on_interrupt)))
{
    ConsoleInterrupt* expected = nullptr;
    if (!g_active.compare_exchange_strong(
            expected,
            this,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
        return;
    }

#ifdef _WIN32
    // CREATE_NO_WINDOW children are not on the parent's console, so
    // GenerateConsoleCtrlEvent never reaches them. When we have no console
    // window, detach any private console and attach to the parent.
    if (GetConsoleWindow() == nullptr)
    {
        FreeConsole();
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            impl_->attached_parent_console = true;
    }

    if (!SetConsoleCtrlHandler(&OnConsoleCtrl, TRUE))
    {
        if (impl_->attached_parent_console)
        {
            FreeConsole();
            impl_->attached_parent_console = false;
        }
        clear_active(this);
        return;
    }
    impl_->handler_installed = true;
    impl_->ready.store(true, std::memory_order_release);
#else
    sigemptyset(&impl_->wait_set);
    sigaddset(&impl_->wait_set, SIGINT);
    if (pthread_sigmask(SIG_BLOCK, &impl_->wait_set, &impl_->previous_mask) != 0)
    {
        clear_active(this);
        return;
    }
    impl_->mask_changed = true;

    try
    {
        impl_->waiter = std::thread([this] { impl_->run_waiter(); });
    }
    catch (std::system_error const&)
    {
        pthread_sigmask(SIG_SETMASK, &impl_->previous_mask, nullptr);
        impl_->mask_changed = false;
        clear_active(this);
        return;
    }

    impl_->ready.store(true, std::memory_order_release);
#endif
}

ConsoleInterrupt::~ConsoleInterrupt()
{
    std::lock_guard<std::mutex> lock(g_handler_mutex);
    clear_active(this);
}

bool ConsoleInterrupt::valid() const
{
    return impl_ && impl_->ready.load(std::memory_order_acquire);
}

bool ConsoleInterrupt::interrupted() const
{
    return impl_ && impl_->interrupted.load(std::memory_order_acquire);
}

void ConsoleInterrupt::request()
{
    if (impl_)
        impl_->fire();
}
