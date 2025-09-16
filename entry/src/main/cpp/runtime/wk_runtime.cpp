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

#include <AbilityKit/native_child_process.h>
#include <AbilityKit/ability_runtime/application_context.h>
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <glib.h>
#include <wpe/webkit.h>

#include "arkts_runtime.h"
#include "environment.h"
#include "log.h"

#include "platform/wpe_display_ohos.h"
#include "wk_web_view.h"

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

// Join any number of values with ':' into a single string.
static void JoinWithColon(const std::vector<std::string>& parts, std::string& out)
{
    out.clear();
    size_t total = 0;
    for (const auto& s : parts) total += s.size() + 1;
    out.reserve(total);

    bool first = true;
    for (const auto& s : parts) {
        if (!first) out.push_back(':');
        out.append(s);
        first = false;
    }
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

void OnNativeChildProcessExit(int32_t pid, int32_t signal)
{
    LOGD("OnNativeChildProcessExit - pid: %{public}d, signal: %{public}d", pid, signal);
}

int64_t WPELaunchProcess(void* /*backend*/, wpe_process_type wpeProcessType, void* userData) noexcept
{
    LOGD("WPELaunchProcess - process type: %{public}d", static_cast<int>(wpeProcessType));
    auto** options = reinterpret_cast<char**>(userData);
    if ((options == nullptr) || (options[0] == nullptr) || (options[1] == nullptr))
        return -1;

    const long long processIdentier = std::strtoll(options[0], nullptr, 10); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
    const int socketFd = std::stoi(options[1]);

    NativeChildProcess_Args args;
    // Insert a node to the head node of the linked list.
    args.fdList.head = (NativeChildProcess_Fd*)malloc(sizeof(NativeChildProcess_Fd));
    args.fdList.head->fdName = strdup(options[0]);
    args.fdList.head->fd = socketFd;
    args.fdList.head->next = nullptr;
    NativeChildProcess_Options process_options = {
        .isolationMode = NCP_ISOLATION_MODE_NORMAL // Needs to be mode normal, otherwise fails to get EGLDisplay
    };

    auto pid = ArkTSRuntime::InvokeSync([=]() -> int64_t {
        int32_t pid_local = -1;

        if (wpeProcessType == WPE_PROCESS_TYPE_WEB) {
            LOGD("Launching web process");
            std::vector<std::string> params;
            params.push_back("WPEWebProcess");
            if (!GetEnvronmentParamsFromApplicationContext(params)) {
                LOGE("Env params failed");
                return static_cast<int64_t>(-1);
            }
            std::string entryParams;
            JoinWithColon(params, entryParams);
            NativeChildProcess_Args localArgs = args; // shallow copy ok for simple POD pointers
            localArgs.entryParams = strdup(entryParams.c_str());

            Ability_NativeChildProcess_ErrCode ret =
                OH_Ability_StartNativeChildProcess("libwebkit_web_process.so:Main",
                                                   localArgs, process_options, &pid_local);

            free(localArgs.entryParams);
        } else if (wpeProcessType == WPE_PROCESS_TYPE_NETWORK) {
            LOGD("Launching network process");
            std::vector<std::string> params;
            params.push_back("WPENetworkProcess");
            if (!GetEnvronmentParamsFromApplicationContext(params)) {
                LOGE("Env params failed");
                return static_cast<int64_t>(-1);
            }
            std::string entryParams;
            JoinWithColon(params, entryParams);
            NativeChildProcess_Args localArgs = args;
            localArgs.entryParams = strdup(entryParams.c_str());

            Ability_NativeChildProcess_ErrCode ret =
                OH_Ability_StartNativeChildProcess("libwebkit_network_process.so:Main",
                                                   localArgs, process_options, &pid_local);

            free(localArgs.entryParams);
        } else {
            LOGE("Unknown process type: %{public}d", static_cast<int>(wpeProcessType));
        }

        LOGD("PID (ArkTS thread): %{public}d", pid_local);
        return static_cast<int64_t>(pid_local);
    });

    // Clean up fd list we allocated before the hop
    if (args.fdList.head) {
        free((void*)args.fdList.head->fdName);
        free(args.fdList.head);
    }

    LOGD("PID (caller thread): %{public}ld", pid);
    return pid;
/*
    int32_t pid = -1;
    if (wpeProcessType == WPE_PROCESS_TYPE_WEB) {
        LOGD("Launching web process");

        std::vector<std::string> params;
        params.push_back("WPEWebProcess");
        GetEnvronmentParamsFromApplicationContext(params);

        std::string entryParams;
        JoinWithColon(params, entryParams);
        args.entryParams = strdup(entryParams.c_str());

        Ability_NativeChildProcess_ErrCode ret = OH_Ability_StartNativeChildProcess(
          "libwpe_web_process.so:Main", args, process_options, &pid);
    } else if (wpeProcessType == WPE_PROCESS_TYPE_NETWORK) {
        LOGD("Launching network process");

        std::vector<std::string> params;
        params.push_back("WPENetworkProcess");
        GetEnvronmentParamsFromApplicationContext(params);

        std::string entryParams;
        JoinWithColon(params, entryParams);
        args.entryParams = strdup(entryParams.c_str());

        Ability_NativeChildProcess_ErrCode ret = OH_Ability_StartNativeChildProcess(
          "libwpe_network_process.so:Main", args, process_options, &pid);
    } else {
        LOGE("Cannot launch process type: %{public}d", static_cast<int>(wpeProcessType));
    }
    LOGD("PID: %{public}d", pid);
    return pid;
*/
}

void WPETerminateProcess(void* /*backend*/, int64_t pid)
{
    LOGD("WPETerminateProcess - pid: %{public}ld", pid);
}

} // namespace

WKRuntime::WKRuntime()
{
    std::vector<std::string> params;
    params.push_back("WPEUIProcess");
    GetEnvronmentParamsFromApplicationContext(params);
    Environment::Initialize(params);
OH_Ability_RegisterNativeChildProcessExitCallback(OnNativeChildProcessExit);
    uiProcessThread_ = std::thread(&WKRuntime::UIProcessThread, this);
}

WKRuntime::~WKRuntime()
{
    LOGD("WKRuntime::~WKRuntime");
    if (uiProcessThread_.joinable()) {
        if (mainLoop_ != nullptr) {
            g_main_loop_quit(*mainLoop_);
        }
        uiProcessThread_.join();
    }

    for (auto& pair : wkWebViewMap_) {
        delete pair.second;
    }
    wkWebViewMap_.clear();
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

    static const wpe_process_provider_interface s_processProviderInterface = {
        .create = nullptr,
        .destroy = nullptr,
        .launch = WPELaunchProcess,
        .terminate = WPETerminateProcess,
        ._wpe_reserved1 = nullptr,
        ._wpe_reserved2 = nullptr,
        ._wpe_reserved3 = nullptr,
        ._wpe_reserved4 = nullptr,
        ._wpe_reserved5 = nullptr
    };
    wpe_process_provider_register_interface(&s_processProviderInterface);

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

void WKRuntime::UIProcessThread()
{
    mainContext_ = std::make_unique<GMainContext*>(g_main_context_new());
    mainLoop_ = std::make_unique<GMainLoop*>(g_main_loop_new(*mainContext_, FALSE));
    g_main_context_push_thread_default(*mainContext_);

    wpeDisplay_ = wpe_display_ohos_new();

    GError* error;
    if (!wpe_display_connect(wpeDisplay_, &error)) {
        LOGE("WKRuntime::UIProcessThread - failed to connect display");
        g_error_free(error);
        return;
    }

    webkit_web_context_new();

    uiReady_.store(true, std::memory_order_release);
    FlushPendingInitsOnUIReady();

    g_main_loop_run(*mainLoop_);

    g_main_context_pop_thread_default(*mainContext_);
    g_main_loop_unref(*mainLoop_);
    g_main_context_unref(*mainContext_);
    mainLoop_ = nullptr;
    mainContext_ = nullptr;
    uiReady_.store(false, std::memory_order_release);
}

void WKRuntime::DoInvoke(void (* callback)(void*), void* callbackData, void (* destroy)(void*))
{
    struct GenericCallback {
        void (* callback)(void*);
        void* callbackData;
        void (* destroy)(void*);
    };

    auto* data = new GenericCallback{callback, callbackData, destroy};
    g_main_context_invoke_full(*mainContext_, G_PRIORITY_DEFAULT, [](void* data) -> gboolean {
        auto* genericData = static_cast<GenericCallback*>(data);
        if (genericData->callback != nullptr) {
            genericData->callback(genericData->callbackData);
        }
        return G_SOURCE_REMOVE;
    }, data, +[](void* data) {
        auto* genericData = static_cast<GenericCallback*>(data);
        if (genericData->destroy != nullptr) {
            genericData->destroy(genericData->callbackData);
        }
        delete genericData;
    });
}
