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

struct _WPEToplevelOHOS {
    WPEToplevel parent;
};

typedef struct {
} WPEToplevelOHOSPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(WPEToplevelOHOS, wpe_toplevel_ohos, WPE_TYPE_TOPLEVEL)


static void WPEToplevelOHOSConstructed(GObject* object)
{
    LOGD("WPEToplevelOHOS::constructed");
    G_OBJECT_CLASS(wpe_toplevel_ohos_parent_class)->constructed(object);

    wpe_toplevel_state_changed(WPE_TOPLEVEL(object), WPE_TOPLEVEL_STATE_ACTIVE);
}

static gboolean WPEToplevelOHOSResize(WPEToplevel* toplevel, int width, int height)
{
    wpe_toplevel_resized(toplevel, width, height);
    wpe_toplevel_foreach_view(toplevel, [](WPEToplevel* toplevel, WPEView* view, gpointer) -> gboolean {
        int width, height;
        wpe_toplevel_get_size(toplevel, &width, &height);
        wpe_view_resized(view, width, height);
        return FALSE;
    }, nullptr);
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
}

WPEToplevel *wpe_toplevel_ohos_new (WPEDisplay *display)
{
    return WPE_TOPLEVEL(g_object_new(WPE_TYPE_TOPLEVEL_OHOS, "display", display, nullptr));
}
