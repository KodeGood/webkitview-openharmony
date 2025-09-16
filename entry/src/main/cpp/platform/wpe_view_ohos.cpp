/**
 * Copyright (C) 2025 Jani Hautakangas <jani@kodegood.com>
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

#include "platform/wpe_view_ohos.h"

#include "log.h"

#include "platform/wpe_view_ohos_renderer.h"

#include <wpe-platform/wpe/WPEBufferOHOS.h>

struct _WPEViewOHOS {
    WPEView parent;

    WPEBuffer* pendingBuffer;
    WPEBuffer* committedBuffer;
    GSource* frameSource;

    std::shared_ptr<WPEViewOHOSRenderer> renderer;
    gint64 lastFrameTime;
};

G_DEFINE_FINAL_TYPE(WPEViewOHOS, wpe_view_ohos, WPE_TYPE_VIEW)

static GSourceFuncs frameSourceFuncs = {
    nullptr, // prepare
    nullptr, // check
    // dispatch
    [](GSource* source, GSourceFunc callback, gpointer userData) -> gboolean
    {
        if (g_source_get_ready_time(source) == -1)
            return G_SOURCE_CONTINUE;
        g_source_set_ready_time(source, -1);
        return callback(userData);
    },
    nullptr, // finalize
    nullptr, // closure_callback
    nullptr, // closure_marshall
};

static void wpeViewOHOSConstructed(GObject* object)
{
    G_OBJECT_CLASS(wpe_view_ohos_parent_class)->constructed(object);

    auto* view = WPE_VIEW_OHOS(object);
    view->frameSource = g_source_new(&frameSourceFuncs, sizeof(GSource));
    g_source_set_priority(view->frameSource, G_PRIORITY_DEFAULT);
    g_source_set_name(view->frameSource, "WPE OHOS frame timer");
    g_source_set_callback(view->frameSource, [](gpointer userData) -> gboolean {
        auto* view = WPE_VIEW(userData);
        auto* viewOHOS = WPE_VIEW_OHOS(view);

        gboolean notifyBufferRendered = FALSE;
        if (viewOHOS->pendingBuffer) { 
            notifyBufferRendered = TRUE;
            if (viewOHOS->committedBuffer) {
                wpe_view_buffer_released(view, viewOHOS->committedBuffer);
                g_object_unref(viewOHOS->committedBuffer);
            }
            viewOHOS->committedBuffer = g_steal_pointer(&viewOHOS->pendingBuffer);
        }

        if (!viewOHOS->committedBuffer)
            return G_SOURCE_CONTINUE;

        GError* bufferError;
        auto eglImage = wpe_buffer_import_to_egl_image(viewOHOS->committedBuffer, &bufferError);
        if (!eglImage) {
            LOGD("WPEViewOHOS::render_buffer - failed to import buffer to EGL image: %s",
                bufferError ? bufferError->message : "unknown error");
            if (bufferError)
                g_error_free(bufferError);
            return G_SOURCE_CONTINUE;
        }

        if (viewOHOS->renderer)
            viewOHOS->renderer->Render(eglImage);

        if (notifyBufferRendered)
            wpe_view_buffer_rendered(view, viewOHOS->committedBuffer);

        if (g_source_is_destroyed(viewOHOS->frameSource))
            return G_SOURCE_REMOVE;
        return G_SOURCE_CONTINUE;
    }, object, nullptr);
    g_source_attach(view->frameSource, g_main_context_get_thread_default());
    g_source_set_ready_time(view->frameSource, -1);
}

static gboolean wpeViewOHOSRenderBuffer(
    WPEView* view, WPEBuffer* buffer, const WPERectangle* /*damageRects*/, guint nDamageRects, GError** error)
{
    g_return_val_if_fail(WPE_IS_VIEW_OHOS(view), FALSE);

    GError* bufferError;
    auto* eglDisplay = wpe_display_get_egl_display(wpe_view_get_display(view), &bufferError);
    if (!eglDisplay) {
        g_set_error(error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED, "Failed to render buffer: can't render buffer because failed to get EGL display: %s", bufferError->message);
        g_error_free(bufferError);
        return FALSE;
    }

    auto* viewOHOS = WPE_VIEW_OHOS(view);
    g_set_object(&viewOHOS->pendingBuffer, buffer);

    // TODO: Maybe could call render directly as we are in the main loop already?
    // However, schedule next frame to follow the style that other platforms use.
    auto now = g_get_monotonic_time();
    if (!viewOHOS->lastFrameTime)
        viewOHOS->lastFrameTime = now;
    auto next = viewOHOS->lastFrameTime + (G_USEC_PER_SEC / 60);
    viewOHOS->lastFrameTime = now;
    if (next <= now)
        g_source_set_ready_time(viewOHOS->frameSource, 0);
    else
        g_source_set_ready_time(viewOHOS->frameSource, next);

    return TRUE;
}

static gboolean wpeViewCanBeMapped(WPEView* view)
{
    LOGD("WPEViewOHOS::can_be_mapped(%p)", view);
    return TRUE;
}

static void wpeViewOHOSDispose(GObject* object)
{
    LOGD("WPEViewOHOS::dispose(%p)", object);

    auto* viewOHOS = WPE_VIEW_OHOS(object);

    if (viewOHOS->frameSource) {
        g_source_destroy(viewOHOS->frameSource);
        viewOHOS->frameSource = nullptr;
    }
    g_clear_object(&viewOHOS->pendingBuffer);
    g_clear_object(&viewOHOS->committedBuffer);
    viewOHOS->renderer.reset();

    G_OBJECT_CLASS(wpe_view_ohos_parent_class)->dispose(object);
}

static void wpe_view_ohos_class_init(WPEViewOHOSClass* klass)
{
    GObjectClass* objectClass = G_OBJECT_CLASS(klass);
    objectClass->constructed = wpeViewOHOSConstructed;
    objectClass->dispose = wpeViewOHOSDispose;

    WPEViewClass* viewClass = WPE_VIEW_CLASS(klass);
    viewClass->render_buffer = wpeViewOHOSRenderBuffer;
    viewClass->can_be_mapped = wpeViewCanBeMapped;
}

static void wpe_view_ohos_init(WPEViewOHOS* view)
{
    LOGD("WPEViewOHOS::init(%p)", view);

    view->pendingBuffer = nullptr;
    view->committedBuffer = nullptr;
    view->frameSource = nullptr;
    view->renderer = nullptr;
    view->lastFrameTime = 0;
}

WPEView* wpe_view_ohos_new(WPEDisplay* display)
{
    return WPE_VIEW(g_object_new(WPE_TYPE_VIEW_OHOS, "display", display, nullptr));
}

void wpe_view_ohos_resize( WPEViewOHOS* view, int width, int height)
{
    g_return_if_fail(WPE_IS_VIEW_OHOS(view));

    LOGD("WPEViewOHOS::resize(%p, %d, %d)", view, width, height);
    wpe_view_resized(WPE_VIEW(view), width, height);
}

void wpe_view_ohos_set_renderer(WPEViewOHOS* view, std::shared_ptr<WPEViewOHOSRenderer> renderer)
{
    g_return_if_fail(WPE_IS_VIEW_OHOS(view));

    LOGD("WPEViewOHOS::set_renderer(%p, %p)", view, renderer.get());

    view->renderer = renderer;
}

void wpe_view_ohos_dispatch_touch_event(WPEViewOHOS* view, OH_NativeXComponent_TouchEvent* event)
{
    g_return_if_fail(view != nullptr);
    g_return_if_fail(event != nullptr);

    WPEEventType eventType = WPE_EVENT_NONE;
    switch (event->type) {
        case OH_NATIVEXCOMPONENT_DOWN:
            eventType = WPE_EVENT_TOUCH_DOWN;
            break;
        case OH_NATIVEXCOMPONENT_UP:
            eventType = WPE_EVENT_TOUCH_UP;
            break;
        case OH_NATIVEXCOMPONENT_MOVE:
            eventType = WPE_EVENT_TOUCH_MOVE;
            break;
        case OH_NATIVEXCOMPONENT_CANCEL:
            eventType = WPE_EVENT_TOUCH_CANCEL;
            break;
        default:
            break;
    }

    for (int i=0; i < event->numPoints; i++) {
        auto* wpeEvent = wpe_event_touch_new(
            eventType,
            WPE_VIEW(view),
            WPE_INPUT_SOURCE_TOUCHSCREEN,
            event->timeStamp,
            static_cast<WPEModifiers>(0),
            event->id,
            event->touchPoints[i].x,
            event->touchPoints[i].y
        );
        wpe_view_event(WPE_VIEW(view), wpeEvent);
        wpe_event_unref(wpeEvent);
    }
}

