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

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <napi/native_api.h>

#include <wpe/webkit.h>

class WKWebView;

class WKRuntime final {
public:
    static bool Export(napi_env env, napi_value exports);

    static std::string GetXComponentId(OH_NativeXComponent *component);

    static WPEDisplay* GetWPEDisplay();

    static WKWebView* GetWebView(const std::string& id);

    static void RequestWebViewInit(const std::string& id);

    static void Invoke(void (* callback)(void*), void* callbackData, void (* destroy)(void*));

private:

    static WKRuntime& GetInstance() noexcept
    {
        static WKRuntime s_singleton;
        return s_singleton;
    }

    WKRuntime();
    ~WKRuntime();

    void RegisterNativeXComponent(const std::string& id, OH_NativeXComponent* nativeXComponent);
    WPEDisplay* GetWPEDisplayInternal() const;
    WKWebView* GetWebViewInternal(const std::string& id);

    void DoRequestWebViewInit(const std::string& id);
    void FlushPendingInitsOnUIReady();

    void UIProcessThread();

    void DoInvoke(void (* callback)(void*), void* callbackData, void (* destroy)(void*));

    std::thread uiProcessThread_;
    std::unique_ptr<GMainContext*> mainContext_ = nullptr;
    std::unique_ptr<GMainLoop*> mainLoop_ = nullptr;
    std::atomic<bool> uiReady_{false};

    std::mutex pendingInitMutex_;
    std::vector<std::string> pendingInitialization_;

    WPEDisplay* wpeDisplay_ = nullptr;

    std::unordered_map<std::string, OH_NativeXComponent*> nativeXComponentMap_;
    std::unordered_map<std::string, WKWebView*> wkWebViewMap_;
};

