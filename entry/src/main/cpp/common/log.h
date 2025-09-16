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

#include <hilog/log.h>

#define WEBKITVIEW_LOG_DOMAIN 0xD9C7
constexpr const char* WEBKITVIEW_LOG_TAG = "WebKitView";

#ifndef LOGI
#define LOGI(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, WEBKITVIEW_LOG_DOMAIN, WEBKITVIEW_LOG_TAG, __VA_ARGS__))
#endif

#ifndef LOGD
#define LOGD(...) ((void)OH_LOG_Print(LOG_APP, LOG_DEBUG, WEBKITVIEW_LOG_DOMAIN, WEBKITVIEW_LOG_TAG, __VA_ARGS__))
#endif

#ifndef LOGE
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, WEBKITVIEW_LOG_DOMAIN, WEBKITVIEW_LOG_TAG, __VA_ARGS__))
#endif
