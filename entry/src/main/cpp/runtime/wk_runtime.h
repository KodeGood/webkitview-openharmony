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
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <napi/native_api.h>
#include <uv.h>

#include <wpe/webkit.h>

class MessagePump;
class WKWebView;

class WKRuntime final {
public:
    // Sets up the UIProcess environment and drives WebKit's GLib run loop from
    // the given libuv loop (the ArkTS main-thread event loop). Must be called
    // once, on the thread that owns `loop`.
    static void Initialize(uv_loop_t* loop);

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

    void DoInitialize(uv_loop_t* loop);

    void DoRequestWebViewInit(const std::string& id);
    void FlushPendingInitsOnUIReady();

    void DoInvoke(void (* callback)(void*), void* callbackData, void (* destroy)(void*));
    void DispatchInvoke(void (* callback)(void*), void* callbackData, void (* destroy)(void*));
    void FlushPendingInvokesOnUIReady();

    struct PendingInvoke {
        void (* callback)(void*);
        void* callbackData;
        void (* destroy)(void*);
    };

    void FailInitialize();

    std::unique_ptr<MessagePump> messagePump_;
    // Set when DoInitialize first runs (attempted), regardless of outcome.
    // Only touched on the ArkTS thread.
    bool initialized_ = false;
    std::atomic<bool> uiReady_{false};
    std::atomic<bool> initFailed_{false};

    std::mutex pendingInitMutex_;
    std::vector<std::string> pendingInitialization_;

    std::mutex pendingInvokeMutex_;
    std::vector<PendingInvoke> pendingInvokes_;

    WPEDisplay* wpeDisplay_ = nullptr;

    std::unordered_map<std::string, OH_NativeXComponent*> nativeXComponentMap_;
    std::unordered_map<std::string, WKWebView*> wkWebViewMap_;
};

