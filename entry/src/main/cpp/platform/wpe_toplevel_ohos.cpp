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

#include "wpe_toplevel_ohos.h"

#include "log.h"

#include <cmath>

struct _WPEToplevelOHOS {
    WPEToplevel parent;

    int physicalWidth;
    int physicalHeight;
};

typedef struct {
} WPEToplevelOHOSPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(WPEToplevelOHOS, wpe_toplevel_ohos, WPE_TYPE_TOPLEVEL)

static WPEScreen* WPEToplevelOHOSGetScreen(WPEToplevel* toplevel)
{
    WPEDisplay* display = wpe_toplevel_get_display(toplevel);
    return display ? wpe_display_get_screen(display, 0) : nullptr;
}

static void WPEToplevelOHOSResizeViews(WPEToplevel* toplevel)
{
    wpe_toplevel_foreach_view(toplevel, [](WPEToplevel* toplevel, WPEView* view, gpointer) -> gboolean {
        int width, height;
        wpe_toplevel_get_size(toplevel, &width, &height);
        if (width > 0 && height > 0)
            wpe_view_resized(view, width, height);
        return FALSE;
    }, nullptr);
}

// The toplevel follows the scale of the screen and turns the physical size of the surface into
// the logical size of the toplevel and its views; WebKit renders them at the scale.
static void WPEToplevelOHOSSyncScaleAndSize(WPEToplevelOHOS* toplevelOHOS)
{
    auto* toplevel = WPE_TOPLEVEL(toplevelOHOS);
    WPEScreen* screen = WPEToplevelOHOSGetScreen(toplevel);
    double scale = screen ? wpe_screen_get_scale(screen) : 1;
    if (scale <= 0)
        scale = 1;
    if (wpe_toplevel_get_scale(toplevel) != scale)
        wpe_toplevel_scale_changed(toplevel, scale);

    int logicalWidth = static_cast<int>(std::lround(toplevelOHOS->physicalWidth / scale));
    int logicalHeight = static_cast<int>(std::lround(toplevelOHOS->physicalHeight / scale));
    LOGD("WPEToplevelOHOS::sync(%p) - %{public}d x %{public}d px at scale %{public}f -> %{public}d x %{public}d",
        toplevel, toplevelOHOS->physicalWidth, toplevelOHOS->physicalHeight, scale, logicalWidth, logicalHeight);
    wpe_toplevel_resized(toplevel, logicalWidth, logicalHeight);
    WPEToplevelOHOSResizeViews(toplevel);
}

static void WPEToplevelOHOSConstructed(GObject* object)
{
    LOGD("WPEToplevelOHOS::constructed");
    G_OBJECT_CLASS(wpe_toplevel_ohos_parent_class)->constructed(object);

    auto* toplevel = WPE_TOPLEVEL_OHOS(object);
    if (auto* screen = WPEToplevelOHOSGetScreen(WPE_TOPLEVEL(toplevel))) {
        g_signal_connect_object(screen, "notify::scale", G_CALLBACK(+[](WPEToplevelOHOS* toplevel, GParamSpec*, WPEScreen*) {
            WPEToplevelOHOSSyncScaleAndSize(toplevel);
        }), toplevel, G_CONNECT_SWAPPED);
    }
    WPEToplevelOHOSSyncScaleAndSize(toplevel);

    wpe_toplevel_state_changed(WPE_TOPLEVEL(object), WPE_TOPLEVEL_STATE_ACTIVE);
}

static gboolean WPEToplevelOHOSResize(WPEToplevel* toplevel, int width, int height)
{
    wpe_toplevel_resized(toplevel, width, height);
    WPEToplevelOHOSResizeViews(toplevel);
    return TRUE;
}

static void wpe_toplevel_ohos_init(WPEToplevelOHOS* toplevel)
{
    LOGD("WPEToplevelOHOS::init(%p)", toplevel);
}

static void wpe_toplevel_ohos_class_init(WPEToplevelOHOSClass* toplevelOHOSClass)
{
    GObjectClass* objectClass = G_OBJECT_CLASS(toplevelOHOSClass);
    objectClass->constructed = WPEToplevelOHOSConstructed;

    WPEToplevelClass* toplevelClass = WPE_TOPLEVEL_CLASS(toplevelOHOSClass);
    toplevelClass->resize = WPEToplevelOHOSResize;
    toplevelClass->get_screen = WPEToplevelOHOSGetScreen;
}

WPEToplevel *wpe_toplevel_ohos_new (WPEDisplay *display)
{
    return WPE_TOPLEVEL(g_object_new(WPE_TYPE_TOPLEVEL_OHOS, "display", display, nullptr));
}

void wpe_toplevel_ohos_set_physical_size(WPEToplevelOHOS* toplevel, int width, int height)
{
    g_return_if_fail(WPE_IS_TOPLEVEL_OHOS(toplevel));

    toplevel->physicalWidth = width;
    toplevel->physicalHeight = height;
    WPEToplevelOHOSSyncScaleAndSize(toplevel);
}
