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

#include "wpe_display_ohos.h"

#include <EGL/egl.h>

#include "log.h"

#include "platform/wpe_toplevel_ohos.h"
#include "platform/wpe_view_ohos.h"

struct _WPEDisplayOHOS {
    WPEDisplay parent;

    EGLDisplay eglDisplay;
};

G_DEFINE_FINAL_TYPE(WPEDisplayOHOS, wpe_display_ohos, WPE_TYPE_DISPLAY)

static gboolean WPEDisplayOHOSConnect(WPEDisplay* display, GError** error)
{
    LOGD("WPEDisplayOHOS::connect(%p)", display);
    auto* displayOHOS = WPE_DISPLAY_OHOS(display);

    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        EGLint eglError = eglGetError();
        LOGE("WPEDisplayAndroid::get_egl_display - eglGetDisplay failed with error 0x%x", eglError);
        g_set_error_literal(error, WPE_DISPLAY_ERROR, WPE_DISPLAY_ERROR_CONNECTION_FAILED, "Failed to get EGL display");
        return FALSE;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(eglDisplay, &major, &minor)) {
        EGLint eglError = eglGetError();
        LOGE("WPEDisplayAndroid::get_egl_display - eglInitialize failed with error 0x%x", eglError);
        g_set_error_literal(error, WPE_DISPLAY_ERROR, WPE_DISPLAY_ERROR_CONNECTION_FAILED, "Failed to initialize EGL");
        return FALSE;
    }

    LOGD("WPEDisplayAndroid::get_egl_display - EGL initialized: version %{public}d.%{public}d", major, minor);
    displayOHOS->eglDisplay = eglDisplay;

    return TRUE;
}

static WPEView* WPEDisplayOHOSCreateView(WPEDisplay* display)
{
    LOGD("WPEDisplayOHOS::create_view(%p)", display);
    auto* view = wpe_view_ohos_new(display);

    WPEToplevel* toplevel = wpe_toplevel_ohos_new(display);
    wpe_view_set_toplevel(view, toplevel);

    return view;
}

static gpointer WPEDisplayOHOSGetEGLDisplay(WPEDisplay* display, GError** error)
{
    LOGD("WPEDisplayOHOS::get_egl_display(%p)", display);

    return WPE_DISPLAY_OHOS(display)->eglDisplay;
}

static WPEBufferDMABufFormats* WPEDisplayOHOSGetPreferredDMABufFormats(WPEDisplay* /*display*/)
{
    LOGD("WPEDisplayOHOS::get_preferred_dma_buf_formats");
    static const struct {
        uint32_t fourcc;
        uint64_t modifier;
    } formats[] = {
        {0x34324152, 0}, // DRM_FORMAT_RGBA8888
     //   {0x34325852, 0}, // DRM_FORMAT_RGBX8888
     //   {0x34324752, 0}, // DRM_FORMAT_RGB888
     //   {0x36314752, 0}, // DRM_FORMAT_RGB565
    };

    auto* builder = wpe_buffer_dma_buf_formats_builder_new(nullptr);
    wpe_buffer_dma_buf_formats_builder_append_group(builder, nullptr, WPE_BUFFER_DMA_BUF_FORMAT_USAGE_RENDERING);

    for (const auto& format : formats) {
        wpe_buffer_dma_buf_formats_builder_append_format(builder, format.fourcc, format.modifier);
    }

    return wpe_buffer_dma_buf_formats_builder_end(builder);
}

static void WPEDisplayOHOSDispose(GObject* object)
{
    LOGD("WPEDisplayOHOS::dispose(%p)", object);

    auto* displayOHOS = WPE_DISPLAY_OHOS(object);

    if (displayOHOS->eglDisplay != nullptr) {
        eglTerminate(displayOHOS->eglDisplay);
        displayOHOS->eglDisplay = nullptr;
    }

    G_OBJECT_CLASS(wpe_display_ohos_parent_class)->dispose(object);
}

static void wpe_display_ohos_class_init(WPEDisplayOHOSClass* klass)
{
    GObjectClass* objectClass = G_OBJECT_CLASS(klass);
    objectClass->dispose = WPEDisplayOHOSDispose;

    WPEDisplayClass* displayClass = WPE_DISPLAY_CLASS(klass);
    displayClass->connect = WPEDisplayOHOSConnect;
    displayClass->create_view = WPEDisplayOHOSCreateView;
    displayClass->get_egl_display = WPEDisplayOHOSGetEGLDisplay;
    displayClass->get_preferred_dma_buf_formats = WPEDisplayOHOSGetPreferredDMABufFormats;
}

static void wpe_display_ohos_init(WPEDisplayOHOS* display)
{
    LOGD("WPEDisplayOHOS::init(%p)", display);

    auto inputDevices = static_cast<WPEAvailableInputDevices>(
        WPE_AVAILABLE_INPUT_DEVICE_TOUCHSCREEN | WPE_AVAILABLE_INPUT_DEVICE_KEYBOARD);
    wpe_display_set_available_input_devices(WPE_DISPLAY(display), inputDevices);
}

WPEDisplay* wpe_display_ohos_new(void)
{
    LOGD("WPEDisplayOHOS::new");
    return WPE_DISPLAY(g_object_new(WPE_TYPE_DISPLAY_OHOS, nullptr));
}

