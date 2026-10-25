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

#include "platform/wpe_screen_ohos.h"

#include "log.h"

#include <cmath>
#include <window_manager/oh_display_manager.h>

struct _WPEScreenOHOS {
    WPEScreen parent;
};

G_DEFINE_FINAL_TYPE(WPEScreenOHOS, wpe_screen_ohos, WPE_TYPE_SCREEN)

static void wpe_screen_ohos_class_init(WPEScreenOHOSClass*)
{
}

static void wpe_screen_ohos_init(WPEScreenOHOS* screen)
{
    wpe_screen_set_scale(WPE_SCREEN(screen), 1.0);
    wpe_screen_set_refresh_rate(WPE_SCREEN(screen), 60000);
}

WPEScreen* wpe_screen_ohos_new(void)
{
    auto* screen = WPE_SCREEN(g_object_new(WPE_TYPE_SCREEN_OHOS, "id", 1, nullptr));
    wpe_screen_ohos_update(WPE_SCREEN_OHOS(screen));
    return screen;
}

void wpe_screen_ohos_update(WPEScreenOHOS* screenOHOS)
{
    g_return_if_fail(WPE_IS_SCREEN_OHOS(screenOHOS));
    auto* screen = WPE_SCREEN(screenOHOS);

    // The virtual pixel ratio is the ArkUI density (vp to px), which ArkWeb also uses as its
    // device scale factor.
    float scale = 0;
    if (OH_NativeDisplayManager_GetDefaultDisplayVirtualPixelRatio(&scale) != DISPLAY_MANAGER_OK || scale <= 0) {
        LOGE("WPEScreenOHOS::update - failed to get the virtual pixel ratio");
        scale = wpe_screen_get_scale(screen);
    }

    int32_t width = 0;
    int32_t height = 0;
    if (OH_NativeDisplayManager_GetDefaultDisplayWidth(&width) == DISPLAY_MANAGER_OK
        && OH_NativeDisplayManager_GetDefaultDisplayHeight(&height) == DISPLAY_MANAGER_OK && width > 0 && height > 0) {
        // Like the other WPE platforms, the screen size is in logical pixels.
        wpe_screen_set_size(screen, static_cast<int>(std::lround(width / scale)), static_cast<int>(std::lround(height / scale)));

        float xDpi = 0;
        float yDpi = 0;
        if (OH_NativeDisplayManager_GetDefaultDisplayDensityXdpi(&xDpi) == DISPLAY_MANAGER_OK
            && OH_NativeDisplayManager_GetDefaultDisplayDensityYdpi(&yDpi) == DISPLAY_MANAGER_OK && xDpi > 0 && yDpi > 0) {
            static constexpr double millimetresPerInch = 25.4;
            wpe_screen_set_physical_size(screen, static_cast<int>(std::lround(width / xDpi * millimetresPerInch)),
                static_cast<int>(std::lround(height / yDpi * millimetresPerInch)));
        }
    }

    uint32_t refreshRate = 0;
    if (OH_NativeDisplayManager_GetDefaultDisplayRefreshRate(&refreshRate) == DISPLAY_MANAGER_OK && refreshRate > 0)
        wpe_screen_set_refresh_rate(screen, static_cast<int>(refreshRate * 1000));

    LOGD("WPEScreenOHOS::update - %{public}d x %{public}d px, scale %{public}f, %{public}u Hz", width, height, scale, refreshRate);
    wpe_screen_set_scale(screen, scale);
}
