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

#include "message_pump.h"

#include <cstdlib>

#include "log.h"

namespace {

int glibEventsToUvEvents(gushort events) noexcept
{
    int uvEvents = 0;
    if ((events & G_IO_IN) != 0)
        uvEvents |= UV_READABLE;
    if ((events & G_IO_OUT) != 0)
        uvEvents |= UV_WRITABLE;
    if ((events & G_IO_PRI) != 0)
        uvEvents |= UV_PRIORITIZED;
    // Always listen for hangups so GLib sees peer-close on its fds.
    uvEvents |= UV_DISCONNECT;
    return uvEvents;
}

gushort uvEventsToGlibEvents(int status, int events) noexcept
{
    if (status < 0)
        return G_IO_ERR;

    gushort glibEvents = 0;
    if ((events & UV_READABLE) != 0)
        glibEvents |= G_IO_IN;
    if ((events & UV_WRITABLE) != 0)
        glibEvents |= G_IO_OUT;
    if ((events & UV_PRIORITIZED) != 0)
        glibEvents |= G_IO_PRI;
    if ((events & UV_DISCONNECT) != 0)
        glibEvents |= G_IO_HUP;
    return glibEvents;
}

} // namespace

MessagePump::MessagePump(uv_loop_t* loop)
    : m_loop(loop)
{
    // Drive the same context WebKit's RunLoop::main uses on this (main) thread:
    // with no thread-default context pushed, that is the default context.
    m_context = g_main_context_ref(g_main_context_default());
    if (!g_main_context_acquire(m_context))
        LOGE("MessagePump: failed to acquire the default GMainContext (owned by another thread?)");

    m_prepare = static_cast<uv_prepare_t*>(std::calloc(1, sizeof(uv_prepare_t)));
    m_check = static_cast<uv_check_t*>(std::calloc(1, sizeof(uv_check_t)));
    m_timer = static_cast<uv_timer_t*>(std::calloc(1, sizeof(uv_timer_t)));
    m_async = static_cast<uv_async_t*>(std::calloc(1, sizeof(uv_async_t)));

    uv_prepare_init(m_loop, m_prepare);
    uv_check_init(m_loop, m_check);
    uv_timer_init(m_loop, m_timer);
    uv_async_init(m_loop, m_async, nullptr);

    m_prepare->data = this;
    m_check->data = this;
    m_timer->data = this;
    m_async->data = this;

    uv_prepare_start(m_prepare, &MessagePump::onPrepare);
    uv_check_start(m_check, &MessagePump::onCheck);

    // Our handles are passive participants: they must service GLib while the
    // loop runs, but never keep the loop (i.e. the app) alive on their own.
    // Unref'ing does not stop them firing, it only removes them from the
    // loop's "alive" accounting.
    uv_unref(reinterpret_cast<uv_handle_t*>(m_prepare));
    uv_unref(reinterpret_cast<uv_handle_t*>(m_check));
    uv_unref(reinterpret_cast<uv_handle_t*>(m_timer));
    uv_unref(reinterpret_cast<uv_handle_t*>(m_async));

    // Initial kick: on OHOS the main-thread uv loop is EventHandler-embedded and
    // only runs when its backend fd signals. The pump is constructed outside any
    // uv iteration (napi Init), so nothing would run the first prepare() that arms
    // the GLib fds/timeout. uv_async_send signals the loop's own eventfd to start it.
    uv_async_send(m_async);
}

MessagePump::~MessagePump()
{
    // Deliberately no final dispatch: check/dispatch without a fresh prepare
    // could re-deliver stale revents, and running arbitrary WebKit callbacks
    // during teardown (after WKRuntime has deleted its views) is unsafe.

    for (auto& entry : m_pollHandles) {
        uv_poll_stop(entry.second);
        uv_close(reinterpret_cast<uv_handle_t*>(entry.second), &MessagePump::onPollClose);
    }
    m_pollHandles.clear();

    uv_prepare_stop(m_prepare);
    uv_check_stop(m_check);
    uv_timer_stop(m_timer);
    uv_close(reinterpret_cast<uv_handle_t*>(m_prepare), &MessagePump::onSimpleClose);
    uv_close(reinterpret_cast<uv_handle_t*>(m_check), &MessagePump::onSimpleClose);
    uv_close(reinterpret_cast<uv_handle_t*>(m_timer), &MessagePump::onSimpleClose);
    uv_close(reinterpret_cast<uv_handle_t*>(m_async), &MessagePump::onSimpleClose);
    m_prepare = nullptr;
    m_check = nullptr;
    m_timer = nullptr;
    m_async = nullptr;

    if (m_pollFds != nullptr) {
        g_free(m_pollFds);
        m_pollFds = nullptr;
    }
    m_pollFdsSize = 0;
    m_pollFdsCapacity = 0;

    g_main_context_release(m_context);
    g_main_context_unref(m_context);
    m_context = nullptr;
}

void MessagePump::onSimpleClose(uv_handle_t* handle)
{
    std::free(handle);
}

void MessagePump::onPollClose(uv_handle_t* handle)
{
    delete static_cast<PollContext*>(handle->data);
    std::free(handle);
}

void MessagePump::onPrepare(uv_prepare_t* handle)
{
    static_cast<MessagePump*>(handle->data)->prepare();
}

void MessagePump::onCheck(uv_check_t* handle)
{
    static_cast<MessagePump*>(handle->data)->dispatch();
}

void MessagePump::onTimer(uv_timer_t*)
{
    // No work here: the timer only bounds the poll wait so that the following
    // "check" phase runs g_main_context_check/dispatch on time.
}

void MessagePump::onPoll(uv_poll_t* handle, int status, int events)
{
    auto* ctx = static_cast<PollContext*>(handle->data);
    MessagePump* self = ctx->pump;
    const int fd = ctx->fd;

    const gushort revents = uvEventsToGlibEvents(status, events);

    // A single fd may back several GPollFD entries (different sources); update
    // every entry, masking to the events each one actually requested.
    for (gint i = 0; i < self->m_pollFdsSize; ++i) {
        if (self->m_pollFds[i].fd == fd) {
            const gushort mask = self->m_pollFds[i].events | G_IO_ERR | G_IO_HUP | G_IO_NVAL;
            self->m_pollFds[i].revents |= (revents & mask);
        }
    }
}

void MessagePump::prepare() noexcept
{
    g_main_context_prepare(m_context, &m_maxPriority);

    if (m_pollFds == nullptr) {
        m_pollFdsCapacity = 1; // There is always at least the context wakeup fd.
        m_pollFds = g_new(GPollFD, m_pollFdsCapacity);
    }

    gint timeout = -1;
    while ((m_pollFdsSize = g_main_context_query(m_context, m_maxPriority, &timeout, m_pollFds, m_pollFdsCapacity))
        > m_pollFdsCapacity) {
        g_free(m_pollFds);
        m_pollFdsCapacity = m_pollFdsSize;
        m_pollFds = g_new(GPollFD, m_pollFdsCapacity);
    }

    // Reset revents; onPoll fills them in during the poll phase.
    for (gint i = 0; i < m_pollFdsSize; ++i)
        m_pollFds[i].revents = 0;

    updatePollHandles();

    // Bound the poll wait by the GLib timeout. timeout < 0 means "wait
    // forever" (until an fd fires); timeout == 0 means "dispatch immediately".
    if (timeout < 0)
        uv_timer_stop(m_timer);
    else
        uv_timer_start(m_timer, &MessagePump::onTimer, static_cast<uint64_t>(timeout), 0);
}

void MessagePump::updatePollHandles() noexcept
{
    // Desired fd -> combined GLib events for this iteration.
    std::unordered_map<int, gushort> desired;
    for (gint i = 0; i < m_pollFdsSize; ++i)
        desired[m_pollFds[i].fd] |= m_pollFds[i].events;

    // Drop handles for fds GLib no longer polls.
    for (auto it = m_pollHandles.begin(); it != m_pollHandles.end();) {
        if (desired.find(it->first) == desired.end()) {
            uv_poll_stop(it->second);
            uv_close(reinterpret_cast<uv_handle_t*>(it->second), &MessagePump::onPollClose);
            it = m_pollHandles.erase(it);
        } else {
            ++it;
        }
    }

    // Add new fds and (re)arm existing ones with the current event mask.
    for (const auto& entry : desired) {
        const int fd = entry.first;
        const int uvEvents = glibEventsToUvEvents(entry.second);

        uv_poll_t* handle = nullptr;
        auto it = m_pollHandles.find(fd);
        if (it == m_pollHandles.end()) {
            handle = static_cast<uv_poll_t*>(std::calloc(1, sizeof(uv_poll_t)));
            if (uv_poll_init(m_loop, handle, fd) != 0) {
                LOGE("MessagePump: uv_poll_init failed for fd %{public}d", fd);
                std::free(handle);
                continue;
            }
            handle->data = new PollContext { this, fd };
            uv_unref(reinterpret_cast<uv_handle_t*>(handle));
            m_pollHandles.emplace(fd, handle);
        } else {
            handle = it->second;
        }
        uv_poll_start(handle, uvEvents, &MessagePump::onPoll);
    }
}

void MessagePump::dispatch() noexcept
{
    if (g_main_context_check(m_context, m_maxPriority, m_pollFds, m_pollFdsSize) == TRUE)
        g_main_context_dispatch(m_context);

    // Re-arm: sources attached during dispatch (idle posts, WebKit timeouts)
    // get no wakeup — GLib only signals the wakeup fd for non-owner-thread
    // attaches — and postdate the uv_timer from the pre-dispatch prepare().
    // Running prepare() again arms a fresh timeout/fd set so they fire on time.
    prepare();
}

void MessagePump::invoke(void (*onExec)(void*), void (*onDestroy)(void*), void* userData) const noexcept
{
    struct InvocationInfo {
        void (*m_onExec)(void*);
        void (*m_onDestroy)(void*);
        void* m_userData;
    };

    auto* info = new InvocationInfo { onExec, onDestroy, userData };
    // Attach an explicit idle source instead of g_main_context_invoke_full:
    // invoke_full runs the callback INLINE when the calling thread owns the
    // context (this pump acquires it at construction), which would silently
    // turn same-thread posts into synchronous calls. An attached source is
    // always deferred to a future dispatch, keeping posts asynchronous.
    GSource* source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(
        source,
        +[](gpointer data) -> gboolean {
            auto* invocation = static_cast<InvocationInfo*>(data);
            if (invocation->m_onExec != nullptr)
                invocation->m_onExec(invocation->m_userData);
            return G_SOURCE_REMOVE;
        },
        info,
        +[](gpointer data) -> void {
            auto* invocation = static_cast<InvocationInfo*>(data);
            if (invocation->m_onDestroy != nullptr)
                invocation->m_onDestroy(invocation->m_userData);
            delete invocation;
        });
    g_source_attach(source, m_context);
    g_source_unref(source);

    // GLib does NOT signal the context wakeup fd when the attaching thread owns
    // the context (this pump acquired it on the loop thread), so a same-thread
    // invoke would otherwise sit unnoticed until an unrelated event. Wake both
    // layers explicitly: the GLib wakeup fd (observed by our uv_poll once the
    // first prepare has armed it) and the uv loop itself (works even before the
    // first iteration, and from any thread).
    g_main_context_wakeup(m_context);
    uv_async_send(m_async);
}
