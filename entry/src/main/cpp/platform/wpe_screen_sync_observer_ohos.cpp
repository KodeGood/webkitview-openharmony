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

#include "platform/wpe_screen_sync_observer_ohos.h"

#include "log.h"

#include <native_vsync/native_vsync.h>

struct _WPEScreenSyncObserverOHOS {
    WPEScreenSyncObserver parent;
    GMutex mutex;
    bool active;
    // A frame callback is requested and holds a reference to the observer.
    bool callbackPending;
};

G_DEFINE_FINAL_TYPE(WPEScreenSyncObserverOHOS, wpe_screen_sync_observer_ohos, WPE_TYPE_SCREEN_SYNC_OBSERVER)

namespace {

// Shared by all the observers and never destroyed: the last reference to an observer can go away in a
// frame callback, which runs on the thread OH_NativeVSync_Destroy() would tear down.
OH_NativeVSync* nativeVSync()
{
    static OH_NativeVSync* const vsync = []() -> OH_NativeVSync* {
        static const char name[] = "WPEScreenSync";
        auto* vsync = OH_NativeVSync_Create(name, sizeof(name) - 1);
        if (!vsync)
            LOGE("WPEScreenSyncObserverOHOS: cannot create OH_NativeVSync, WebKit will use a timer for screen sync");
        return vsync;
    }();
    return vsync;
}

// In a frame callback: whether the observer is still active and needs another frame; if not, the
// reference the callback held is released.
bool continueFrameCallbacksOrRelease(WPEScreenSyncObserverOHOS* self)
{
    {
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
        if (self->active)
            return true;
        self->callbackPending = false;
    }
    g_object_unref(self);
    return false;
}

void frameCallback(long long, void* data);

// Takes over the reference the caller holds for the callback.
void requestFrame(WPEScreenSyncObserverOHOS* self)
{
    // Each request queues its own one-shot callback, so the observers can share nativeVSync().
    int error = OH_NativeVSync_RequestFrameWithMultiCallback(nativeVSync(), frameCallback, self);
    if (!error)
        return;

    LOGE("WPEScreenSyncObserverOHOS: OH_NativeVSync_RequestFrameWithMultiCallback failed: %{public}d", error);
    {
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
        self->callbackPending = false;
    }
    g_object_unref(self);
}

void frameCallback(long long, void* data)
{
    auto* self = WPE_SCREEN_SYNC_OBSERVER_OHOS(data);
    if (!continueFrameCallbacksOrRelease(self))
        return;

    // Without the mutex: the sync callbacks can stop or start the observer.
    WPE_SCREEN_SYNC_OBSERVER_CLASS(wpe_screen_sync_observer_ohos_parent_class)->sync(WPE_SCREEN_SYNC_OBSERVER(self));

    if (continueFrameCallbacksOrRelease(self))
        requestFrame(self);
}

} // namespace

static void wpeScreenSyncObserverOHOSStart(WPEScreenSyncObserver* observer)
{
    auto* self = WPE_SCREEN_SYNC_OBSERVER_OHOS(observer);
    {
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
        if (self->active)
            return;
        self->active = true;
        // A callback still pending from before the last stop() carries on.
        if (self->callbackPending)
            return;
        self->callbackPending = true;
    }
    requestFrame(WPE_SCREEN_SYNC_OBSERVER_OHOS(g_object_ref(self)));
}

static void wpeScreenSyncObserverOHOSStop(WPEScreenSyncObserver* observer)
{
    auto* self = WPE_SCREEN_SYNC_OBSERVER_OHOS(observer);
    // This runs from the frame callback too: the pending callback finds the observer stopped and releases it.
    g_autoptr(GMutexLocker) locker = g_mutex_locker_new(&self->mutex);
    self->active = false;
}

static void wpeScreenSyncObserverOHOSFinalize(GObject* object)
{
    g_mutex_clear(&WPE_SCREEN_SYNC_OBSERVER_OHOS(object)->mutex);
    G_OBJECT_CLASS(wpe_screen_sync_observer_ohos_parent_class)->finalize(object);
}

static void wpe_screen_sync_observer_ohos_class_init(WPEScreenSyncObserverOHOSClass* klass)
{
    GObjectClass* objectClass = G_OBJECT_CLASS(klass);
    objectClass->finalize = wpeScreenSyncObserverOHOSFinalize;

    WPEScreenSyncObserverClass* observerClass = WPE_SCREEN_SYNC_OBSERVER_CLASS(klass);
    observerClass->start = wpeScreenSyncObserverOHOSStart;
    observerClass->stop = wpeScreenSyncObserverOHOSStop;
}

static void wpe_screen_sync_observer_ohos_init(WPEScreenSyncObserverOHOS* self)
{
    g_mutex_init(&self->mutex);
    self->active = false;
    self->callbackPending = false;
}

WPEScreenSyncObserver* wpe_screen_sync_observer_ohos_new(void)
{
    if (!nativeVSync())
        return nullptr;
    return WPE_SCREEN_SYNC_OBSERVER(g_object_new(WPE_TYPE_SCREEN_SYNC_OBSERVER_OHOS, nullptr));
}
