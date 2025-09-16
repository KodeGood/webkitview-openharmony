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
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <native_window/external_window.h>
#include <string>
#include <vector>


#include "platform/wpe_view_ohos_renderer.h"

class WPEViewOHOSGLES3Renderer : public WPEViewOHOSRenderer {
public:
    WPEViewOHOSGLES3Renderer();
    ~WPEViewOHOSGLES3Renderer();

    bool Initialize(OHNativeWindow* nativeWindow, int width, int height) override;
    void Cleanup() override;

    void Render(EGLImage image) override;

private:

    bool InitializeEGL();
    GLuint CreateProgram(const char *vertexShader, const char *fragShader);
    GLuint LoadShader(GLenum type, const char *shaderSrc);

    OHNativeWindow* nativeWindow_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_;

    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;
    GLuint programHandle_;
    GLuint texture_;
};

