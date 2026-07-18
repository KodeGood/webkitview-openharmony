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

#include "wk_runtime.h"

#include <AbilityKit/ability_runtime/application_context.h>
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <glib.h>
#include <wpe/webkit.h>

#include <string>
#include <vector>

#include "environment.h"
#include "log.h"
#include "message_pump.h"

#include "platform/wpe_display_ohos.h"
#include "wk_web_view.h"

// On OHOS, WebKit owns process launching (UIProcess/Launcher/ohos/ProcessLauncherOHOS.cpp):
// this runtime registers no wpe_process_provider and only initialises the UIProcess environment.

namespace {

AbilityRuntime_ErrorCode GetDir(AbilityRuntime_ErrorCode (*fn)(char*, int32_t, int32_t*), std::string& outStr)
{
    const int32_t kMax = 64 * 1024;
    int32_t size = 512;
    int32_t written = 0;
    std::vector<char> buf;

    while (size <= kMax) {
        buf.assign(size, '\0');
        AbilityRuntime_ErrorCode ret = fn(buf.data(), size, &written);
        if (ret == ABILITY_RUNTIME_ERROR_CODE_NO_ERROR) {
            buf[size - 1] = '\0';
            outStr.assign(buf.data());
            return ABILITY_RUNTIME_ERROR_CODE_NO_ERROR;
        }
        if (ret == ABILITY_RUNTIME_ERROR_CODE_PARAM_INVALID) {
            size = (written > size && written < kMax) ? (written + 1) : (size * 2);
            continue;
        }
        return ret; // non-resize error
    }
    return ABILITY_RUNTIME_ERROR_CODE_PARAM_INVALID;
}

bool GetEnvronmentParamsFromApplicationContext(std::vector<std::string>& outParams)
{
    std::string cacheDir, filesDir, tempDir, bundleCodeDir;
    int err = GetDir(&OH_AbilityRuntime_ApplicationContextGetCacheDir, cacheDir);
    if (err != ABILITY_RUNTIME_ERROR_CODE_NO_ERROR) {
        LOGE("Failed to get cache dir: %{public}d", static_cast<int>(err));
        return false;
    }

    err = GetDir(&OH_AbilityRuntime_ApplicationContextGetFilesDir, filesDir);
    if (err != ABILITY_RUNTIME_ERROR_CODE_NO_ERROR) {
        LOGE("Failed to get files dir: %{public}d", static_cast<int>(err));
        return false;
    }

    err = GetDir(&OH_AbilityRuntime_ApplicationContextGetTempDir, tempDir);
    if (err != ABILITY_RUNTIME_ERROR_CODE_NO_ERROR) {
        LOGE("Failed to get temp dir: %{public}d", static_cast<int>(err));
        return false;
    }

    err = GetDir(&OH_AbilityRuntime_ApplicationContextGetBundleCodeDir, bundleCodeDir);
    if (err != ABILITY_RUNTIME_ERROR_CODE_NO_ERROR) {
        LOGE("Failed to get bundle code dir: %{public}d", static_cast<int>(err));
        return false;
    }

    outParams.push_back(cacheDir);
    outParams.push_back(filesDir);
    outParams.push_back(tempDir);
    outParams.push_back(bundleCodeDir);
    return true;
}

} // namespace

WKRuntime::WKRuntime() = default;

WKRuntime::~WKRuntime()
{
    LOGD("WKRuntime::~WKRuntime");

    for (auto& pair : wkWebViewMap_) {
        delete pair.second;
    }
    wkWebViewMap_.clear();

    messagePump_ = nullptr;
    uiReady_.store(false, std::memory_order_release);
}

void WKRuntime::Initialize(uv_loop_t* loop)
{
    GetInstance().DoInitialize(loop);
}

void WKRuntime::DoInitialize(uv_loop_t* loop)
{
    // Guard on "attempted", not on uiReady_: a second call (e.g. the napi
    // module Init running for another env) must not construct a second
    // MessagePump over the same default GMainContext.
    if (initialized_)
        return;
    initialized_ = true;

    std::vector<std::string> params;
    params.push_back("WPEUIProcess");
    if (GetEnvronmentParamsFromApplicationContext(params)) {
        Environment::Initialize(params);
    } else {
        // Environment::Initialize indexes params[1..4]; without the dirs it
        // would read out of bounds. WebKit still starts, just without the
        // sandbox-dir environment (fontconfig/GStreamer paths etc.).
        LOGE("WKRuntime::DoInitialize - ApplicationContext dirs unavailable; environment not initialised");
    }

    // Drive WebKit's GLib run loop from this (the ArkTS) thread's libuv loop.
    // WebKit runs on the ArkTS thread, which owns the default GMainContext the
    // pump services.
    messagePump_ = std::make_unique<MessagePump>(loop);

    wpeDisplay_ = wpe_display_ohos_new();

    GError* error = nullptr;
    if (!wpe_display_connect(wpeDisplay_, &error)) {
        LOGE("WKRuntime::DoInitialize - failed to connect display: %{public}s",
            error ? error->message : "unknown error");
        if (error != nullptr)
            g_error_free(error);
        FailInitialize();
        return;
    }

    // Load-bearing: this is the first WTF-touching WebKit call, made on the
    // ArkTS thread with no thread-default GMainContext pushed. It triggers
    // webkitInitialize() -> WTF::initializeMainThread(), claiming this thread
    // as WebKit's main thread and binding RunLoop::main to
    // g_main_context_default() — the context the MessagePump services. If any
    // WTF-touching call ever precedes this on another thread, RunLoop::main
    // binds a private context nobody pumps and WebKit silently hangs.
    // Web views created without an explicit web-context use the default one
    // (get_default is transfer-none, so nothing to unref here).
    webkit_web_context_get_default();

    uiReady_.store(true, std::memory_order_release);
    FlushPendingInvokesOnUIReady();
    FlushPendingInitsOnUIReady();
}

void WKRuntime::FailInitialize()
{
    initFailed_.store(true, std::memory_order_release);

    // Nothing will ever flush the pending queues; run the destroy callbacks so
    // queued payloads are not leaked, and drop queued view inits.
    std::vector<PendingInvoke> invokes;
    {
        std::lock_guard<std::mutex> lock(pendingInvokeMutex_);
        invokes.swap(pendingInvokes_);
    }
    for (const auto& invoke : invokes) {
        if (invoke.destroy != nullptr)
            invoke.destroy(invoke.callbackData);
    }

    {
        std::lock_guard<std::mutex> lock(pendingInitMutex_);
        pendingInitialization_.clear();
    }

    messagePump_ = nullptr;
}

bool WKRuntime::Export(napi_env env, napi_value exports)
{
    LOGD("WKRuntime::Export");
    napi_status status;

    napi_value exportInstance = nullptr;
    OH_NativeXComponent* nativeXComponent = nullptr;

    status = napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance);
    if (status != napi_ok) {
        return false;
    }

    status = napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&nativeXComponent));
    if (status != napi_ok) {
        return false;
    }

    // No wpe_process_provider is registered: on OHOS, WebKit's own ProcessLauncherOHOS spawns
    // the Web/Network processes via AbilityKit and forwards their sandbox environment.

    auto id = WKRuntime::GetXComponentId(nativeXComponent);
    WKRuntime::GetInstance().RegisterNativeXComponent(id, nativeXComponent);

    return true;
}

std::string WKRuntime::GetXComponentId(OH_NativeXComponent *component)
{
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    int32_t ret = OH_NativeXComponent_GetXComponentId(component, idStr, &idSize);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        return std::string();
    }
    return std::string(idStr);
}

WPEDisplay* WKRuntime::GetWPEDisplay()
{
    return GetInstance().GetWPEDisplayInternal();
}

WKWebView* WKRuntime::GetWebView(const std::string& id)
{
    return GetInstance().GetWebViewInternal(id);
}

void WKRuntime::RequestWebViewInit(const std::string& id)
{
    GetInstance().DoRequestWebViewInit(id);
}

void WKRuntime::Invoke(void (* callback)(void*), void* callbackData, void (* destroy)(void*))
{
    GetInstance().DoInvoke(callback, callbackData, destroy);
}

void WKRuntime::RegisterNativeXComponent(const std::string& id, OH_NativeXComponent* nativeXComponent)
{
   if (nativeXComponentMap_.find(id) == nativeXComponentMap_.end()) {
        nativeXComponentMap_[id] = nativeXComponent;
   } else {
        if (nativeXComponentMap_[id] != nativeXComponent) {
            nativeXComponentMap_[id] = nativeXComponent;
        }
   }
   GetWebViewInternal(id)->RegisterCallbacks(nativeXComponent);
}

WPEDisplay* WKRuntime::GetWPEDisplayInternal() const
{
    return wpeDisplay_;
}

WKWebView* WKRuntime::GetWebViewInternal(const std::string& id)
{
    if (wkWebViewMap_.find(id) == wkWebViewMap_.end()) {
        WKWebView* webView = new WKWebView(id);
        wkWebViewMap_[id] = webView;
        return webView;
    }
    return wkWebViewMap_[id];
}

void WKRuntime::DoRequestWebViewInit(const std::string& id)
{
    if (initFailed_.load(std::memory_order_acquire)) {
        LOGE("WKRuntime::DoRequestWebViewInit - runtime initialization failed; ignoring '%{public}s'", id.c_str());
        return;
    }

    if (uiReady_.load(std::memory_order_acquire)) {
        auto* data = new std::string(id);
        DoInvoke(
            [](void* p){
                auto* s = static_cast<std::string*>(p);
                if (auto* wv = WKRuntime::GetInstance().GetWebViewInternal(*s))
                    wv->Init();
            },
            data,
            [](void* p){ delete static_cast<std::string*>(p); }
        );
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pendingInitMutex_);
        if (std::find(pendingInitialization_.begin(), pendingInitialization_.end(), id) == pendingInitialization_.end())
            pendingInitialization_.push_back(id);
    }
}

void WKRuntime::FlushPendingInitsOnUIReady()
{
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(pendingInitMutex_);
        if (pendingInitialization_.empty())
            return;
        ids.swap(pendingInitialization_);
    }

    for (const auto& id : ids) {
        if (auto* wv = GetWebViewInternal(id))
            wv->Init();
    }
}

void WKRuntime::DoInvoke(void (* callback)(void*), void* callbackData, void (* destroy)(void*))
{
    // The message pump (and the GLib context it services) is created in
    // DoInitialize. Until it is ready, queue invokes instead of dispatching to
    // a null pump (which otherwise crashes when ACE fires e.g. OnSurfaceCreated
    // before init).
    if (initFailed_.load(std::memory_order_acquire)) {
        // Initialization failed permanently; don't queue into a void.
        if (destroy != nullptr)
            destroy(callbackData);
        return;
    }

    if (!uiReady_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(pendingInvokeMutex_);
        // Re-check under the lock: FlushPendingInvokesOnUIReady() runs after
        // uiReady_ is set, so anything queued here is guaranteed to be flushed.
        if (!uiReady_.load(std::memory_order_acquire)) {
            pendingInvokes_.push_back({callback, callbackData, destroy});
            return;
        }
    }

    DispatchInvoke(callback, callbackData, destroy);
}

void WKRuntime::FlushPendingInvokesOnUIReady()
{
    std::vector<PendingInvoke> invokes;
    {
        std::lock_guard<std::mutex> lock(pendingInvokeMutex_);
        invokes.swap(pendingInvokes_);
    }

    for (const auto& invoke : invokes) {
        DispatchInvoke(invoke.callback, invoke.callbackData, invoke.destroy);
    }
}

void WKRuntime::DispatchInvoke(void (* callback)(void*), void* callbackData, void (* destroy)(void*))
{
    // Post to the GLib context via the pump; the context wakeup fd is observed
    // by libuv, so the callback runs on the ArkTS/GLib thread.
    messagePump_->invoke(callback, destroy, callbackData);
}
