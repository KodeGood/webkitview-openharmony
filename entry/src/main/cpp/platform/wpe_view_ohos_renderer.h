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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <native_window/external_window.h>

class WPEViewOHOSRenderer {
public:
    ~WPEViewOHOSRenderer() = default;

    virtual bool Initialize(OHNativeWindow* nativeWindow, int width, int height) = 0;
    virtual void Cleanup() = 0;

    virtual void Render(EGLImage eglImage) = 0;
};

