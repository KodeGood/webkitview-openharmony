/**
 * Copyright (C) 2026 Jani Hautakangas <jani@kodegood.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include <glib.h>
#include <uv.h>

#include <unordered_map>

/*
 * MessagePump integrates the GLib main context into a libuv event loop so that
 * the ArkTS/libuv loop (the application's main-thread event loop) drives GLib,
 * and hence WebKit's UIProcess run loop, without a dedicated thread or a nested
 * g_main_loop_run().
 *
 * This is the OpenHarmony counterpart of the Android ALooper-based MessagePump.
 * On OHOS the ArkTS event loop *is* a libuv loop (obtained via
 * napi_get_uv_event_loop), and only public SDK APIs are used (libuv + GLib) —
 * no platform event-handler adapter.
 *
 * One GLib main-context iteration is mapped onto libuv's loop phases:
 *
 *   uv "prepare" phase  ->  g_main_context_prepare + g_main_context_query,
 *                           then arm one uv_poll per GLib fd and a uv_timer for
 *                           the GLib timeout.
 *   uv "poll"    phase  ->  libuv blocks on those fds / the timer.
 *   uv "check"   phase  ->  g_main_context_check + g_main_context_dispatch.
 *
 * All methods must be called on the thread that owns the libuv loop.
 */
class MessagePump final {
public:
    explicit MessagePump(uv_loop_t* loop);

    MessagePump(MessagePump&&) = delete;
    MessagePump& operator=(MessagePump&&) = delete;
    MessagePump(const MessagePump&) = delete;
    MessagePump& operator=(const MessagePump&) = delete;

    ~MessagePump();

    // Queue a callback to run on the GLib (== libuv) thread. Thread-safe: it
    // wakes the GLib context, whose wakeup fd is observed by libuv. Always
    // deferred to a future dispatch, even when called on the loop thread.
    void invoke(void (*onExec)(void*), void (*onDestroy)(void*), void* userData) const noexcept;

private:
    void prepare() noexcept;
    void dispatch() noexcept;
    void updatePollHandles() noexcept;

    static void onPrepare(uv_prepare_t* handle);
    static void onCheck(uv_check_t* handle);
    static void onTimer(uv_timer_t* handle);
    static void onPoll(uv_poll_t* handle, int status, int events);

    static void onSimpleClose(uv_handle_t* handle);
    static void onPollClose(uv_handle_t* handle);

    // Per-poll-handle context; carries the fd since libuv does not expose it.
    struct PollContext {
        MessagePump* pump;
        int fd;
    };

    uv_loop_t* m_loop = nullptr;
    GMainContext* m_context = nullptr;

    uv_prepare_t* m_prepare = nullptr;
    uv_check_t* m_check = nullptr;
    uv_timer_t* m_timer = nullptr;
    // Wakes the uv loop from outside an iteration: needed once at construction
    // (nothing else triggers the first uv_run on the EventHandler-embedded main
    // loop) and after same-thread source attachment (which GLib does not signal
    // to an owner == current-thread context).
    uv_async_t* m_async = nullptr;

    gint m_maxPriority = 0;
    GPollFD* m_pollFds = nullptr;
    gint m_pollFdsSize = 0;
    gint m_pollFdsCapacity = 0;

    // fd -> uv_poll handle currently registered with the loop.
    std::unordered_map<int, uv_poll_t*> m_pollHandles;
};
