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

#pragma once

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <glib-object.h>
#include <memory>
#include <wpe-platform/wpe/wpe-platform.h>

class WPEViewOHOSRenderer;

G_BEGIN_DECLS

#define WPE_TYPE_VIEW_OHOS (wpe_view_ohos_get_type())
G_DECLARE_FINAL_TYPE(WPEViewOHOS, wpe_view_ohos, WPE, VIEW_OHOS, WPEView)


WPEView* wpe_view_ohos_new(WPEDisplay* display);
void wpe_view_ohos_resize(WPEViewOHOS* view, int width, int height);
void wpe_view_ohos_set_renderer(WPEViewOHOS* view, std::shared_ptr<WPEViewOHOSRenderer> renderer);
void wpe_view_ohos_dispatch_touch_event(WPEViewOHOS* view, OH_NativeXComponent_TouchEvent* event);

G_END_DECLS

