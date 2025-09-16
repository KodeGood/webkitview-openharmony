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

#include "platform/gles3/wpe_view_ohos_gles3_renderer.h"

#include "log.h"

namespace {
static const char* s_vertexShaderSource = 
    "attribute vec2 pos;\n"
    "attribute vec2 texture;\n"
    "varying vec2 v_texture;\n"
    "void main() {\n"
    "  v_texture = texture;\n"
    "  gl_Position = vec4(pos, 0, 1);\n"
    "}\n";

static const char* s_fragmentShaderSource =
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "varying vec2 v_texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(u_texture, v_texture);\n"
    "}\n";

void CheckGLError(const char* label)
{
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        const char* errStr = "UNKNOWN";
        switch (err) {
            case GL_INVALID_ENUM:      errStr = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE:     errStr = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: errStr = "GL_INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY:     errStr = "GL_OUT_OF_MEMORY"; break;
#ifdef GL_INVALID_FRAMEBUFFER_OPERATION
            case GL_INVALID_FRAMEBUFFER_OPERATION:
                errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
#endif
        }
        LOGE("GL error at %{public}s: 0x%{public}x (%{public}s)", label, err, errStr);
    }
}

}

WPEViewOHOSGLES3Renderer::WPEViewOHOSGLES3Renderer()
{
}

WPEViewOHOSGLES3Renderer::~WPEViewOHOSGLES3Renderer()
{
    Cleanup();
}

bool WPEViewOHOSGLES3Renderer::Initialize(OHNativeWindow* nativeWindow, int width, int height)
{
    nativeWindow_ = nativeWindow;
    width_ = width;
    height_ = height;

    return InitializeEGL();
}

void WPEViewOHOSGLES3Renderer::Cleanup()
{
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext_ != EGL_NO_CONTEXT) {
            eglDestroyContext(eglDisplay_, eglContext_);
            eglContext_ = EGL_NO_CONTEXT;
        }
        if (eglSurface_ != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay_, eglSurface_);
            eglSurface_ = EGL_NO_SURFACE;
        }
        eglTerminate(eglDisplay_);
        eglDisplay_ = EGL_NO_DISPLAY;
    }
    if (programHandle_) {
        glDeleteProgram(programHandle_);
        programHandle_ = 0;
    }
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
}

void WPEViewOHOSGLES3Renderer::Render(EGLImage image)
{
    if (image == EGL_NO_IMAGE) {
        LOGE("Failed to bind OH_NativeBuffer to an EGLImage.");
        return;
    }
    
    if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
        LOGE("eglMakeCurrent error = %{public}d", eglGetError());
        return;
    }

    glViewport(0,0,width_,height_);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GLint attrPos = glGetAttribLocation(programHandle_, "pos");
    CheckGLError("glGetAttribLocation pos");
    GLint attrTexture = glGetAttribLocation(programHandle_, "texture");
    CheckGLError("glGetAttribLocation texture");
    GLint uniformTexture = glGetUniformLocation(programHandle_, "u_texture");
    CheckGLError("glGetUniformLocation u_texture");

    glUseProgram(programHandle_);
    CheckGLError("glUseProgram");

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glEGLImageTargetTexture2DOES_(GL_TEXTURE_2D, image);
    CheckGLError("pGlEGLImageTargetTexture2DOES");
    glUniform1i(uniformTexture, 0);

    static float positionCoords[] = { -1, 1, 1, 1, -1, -1, 1, -1 };
    static float textureCoords[] = { 0, 0, 1, 0, 0, 1, 1, 1 };

    glVertexAttribPointer(attrPos, 2, GL_FLOAT, GL_FALSE, 0, positionCoords);
    glVertexAttribPointer(attrTexture, 2, GL_FLOAT, GL_FALSE, 0, textureCoords);

    glEnableVertexAttribArray(attrPos);
    glEnableVertexAttribArray(attrTexture);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(attrPos);
    glDisableVertexAttribArray(attrTexture);

    eglSwapBuffers(eglDisplay_, eglSurface_);
}

bool WPEViewOHOSGLES3Renderer::InitializeEGL()
{
    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay_ == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
        return false;
    }
    if (!eglInitialize(eglDisplay_, nullptr, nullptr)) {
        LOGE("Failed to initialize EGL");
        return false;
    }
    eglCreateImageKHR_ =
        reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    if (!eglCreateImageKHR_) {
        LOGE("Missing eglCreateImageKHR");
        return false;
    }

    eglDestroyImageKHR_ =
        reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    if (!eglDestroyImageKHR_) {
        LOGE("Missing eglDestroyImageKHR");
        return false;
    }

    glEGLImageTargetTexture2DOES_ =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!glEGLImageTargetTexture2DOES_) {
        LOGE("Missing glEGLImageTargetTexture2DOES");
        return false;
    }

    static const EGLint configAttributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };

    EGLint count;
    EGLConfig config;
    if (!eglChooseConfig(eglDisplay_, configAttributes, &config, 1, &count)) {
        LOGE("Failed to choose EGL config");
        return false;
    }

    if (count == 0) {
        LOGE("No suitable EGL config found");
        return false;
    }

    static const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };

    eglContext_ = eglCreateContext(eglDisplay_, config, EGL_NO_CONTEXT, contextAttributes);
    if (eglContext_ == EGL_NO_CONTEXT) {
        LOGE("Failed to create EGL context");
        return false;
    }

    EGLNativeWindowType eglWindow = reinterpret_cast<EGLNativeWindowType>(nativeWindow_);
    eglSurface_ = eglCreateWindowSurface(eglDisplay_, config, eglWindow, nullptr);
    if (eglSurface_ == EGL_NO_SURFACE) {
        LOGE("Failed to create EGL window surface");
        return false;
    }

    if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
        LOGE("Failed to make EGL context current");
        return false;
    }

    programHandle_ = CreateProgram(s_vertexShaderSource, s_fragmentShaderSource);
    if (!programHandle_) {
        LOGE("Could not create CreateProgram");
        return false;
    }

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

GLuint WPEViewOHOSGLES3Renderer::CreateProgram(const char *vertexShader, const char *fragShader)
{
    GLuint vertex;
    GLuint fragment;
    GLuint program;
    GLint linked;

    vertex = LoadShader(GL_VERTEX_SHADER, vertexShader);
    if (vertex == 0) {
        LOGE("LoadShader: vertexShader error");
        return 0;
    }

    fragment = LoadShader(GL_FRAGMENT_SHADER, fragShader);
    if (fragment == 0) {
        LOGE("LoadShader: fragShader error");
        glDeleteShader(vertex);
        return 0;
    }

    program = glCreateProgram();
    if (program == 0) {
        LOGE("CreateProgram program error");
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);

    if (!linked) {
        LOGE("CreateProgram linked error");
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            std::string infoLog(infoLen, '\0');
            glGetProgramInfoLog(program, infoLen, nullptr, (GLchar *)&infoLog);
            LOGE("Error linking program:%{public}s\n", infoLog.c_str());
        }
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        glDeleteProgram(program);
        return 0;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

GLuint WPEViewOHOSGLES3Renderer::LoadShader(GLenum type, const char *shaderSrc)
{
    GLuint shader;
    GLint compiled;

    shader = glCreateShader(type);
    if (shader == 0) {
        LOGE("LoadShader shader error");
        return 0;
    }

    glShaderSource(shader, 1, &shaderSrc, nullptr);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);

        if (infoLen > 1) {
            std::string infoLog(infoLen, '\0');
            glGetShaderInfoLog(shader, infoLen, nullptr, (GLchar *)&infoLog);
            LOGE("Error compiling shader:%{public}s\n", infoLog.c_str());
        }

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}
