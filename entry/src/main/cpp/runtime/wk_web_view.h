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

#include <string>
#include <memory>

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <napi/native_api.h>
#include <native_window/external_window.h>

#include <wpe/webkit.h>

class WPEViewOHOSRenderer;
typedef struct _WPEViewOHOS WPEViewOHOS;

class WKWebView final {
public:
    WKWebView(const std::string& id);
    ~WKWebView();

    static bool Export(napi_env env, napi_value exports);

    void RegisterCallbacks(OH_NativeXComponent* component);

    void Init();
    void LoadURL(const std::string& url);

    // ACE XComponent callbacks
    void OnSurfaceCreated(OHNativeWindow* window, int width, int height);
    void OnSurfaceChanged(OHNativeWindow* window, int width, int height);
    void OnSurfaceDestroyed(OHNativeWindow* window);
    void DispatchTouchEvent(OH_NativeXComponent_TouchEvent* touchEvent);

private:

    void InitializeRenderer();


    static void OnLoadChanged(WKWebView* wkWebView, WebKitLoadEvent loadEvent, WebKitWebView* webView) noexcept;
    static int OnLoadFailed(WKWebView* wkWebView, WebKitLoadEvent loadEvent, const char* failingURI, GError* error, WebKitWebView* webView) noexcept;
    static int OnLoadFailedWithTlsErrors(WebKitWebView *web_view,
                                                 char                *failing_uri,
                                                 GTlsCertificate     *certificate,
                                                 GTlsCertificateFlags errors,
                                                 void                *user_data) noexcept;

    std::string id_;
    OH_NativeXComponent_Callback callback_;

    OHNativeWindow* nativeWindow_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    WebKitWebView* webView_ = nullptr;
    WPEViewOHOS* wpeView_ = nullptr;

    std::shared_ptr<WPEViewOHOSRenderer> wpeViewRenderer_ = nullptr;

    std::vector<gulong> signalHandlers_;
};

