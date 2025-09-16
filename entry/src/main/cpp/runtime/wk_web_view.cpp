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

#include "wk_web_view.h"

#include "log.h"
#include "wk_runtime.h"

#include "platform/gles3/wpe_view_ohos_gles3_renderer.h"
#include "platform/wpe_view_ohos.h"

namespace {

void OnSurfaceCreatedCB(OH_NativeXComponent* component, void* window)
{
    auto id = WKRuntime::GetXComponentId(component);
    uint64_t width, height = 0;
    OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);

    struct CallbackData
    {
        std::string id;
        void* window;
        uint64_t width;
        uint64_t height;
    }; 

    auto* data = new CallbackData{ id, window, width, height };
    WKRuntime::Invoke(
        [](void* data) {
            auto* callbackData = static_cast<CallbackData*>(data);
            auto* webView = WKRuntime::GetWebView(callbackData->id);
            if (webView != nullptr) {
                webView->OnSurfaceCreated(
                    static_cast<OHNativeWindow*>(callbackData->window),
                    static_cast<int>(callbackData->width),
                    static_cast<int>(callbackData->height)
                );  
            }
        },
        data,
        [](void* data) {
            delete static_cast<CallbackData*>(data);
        }
    );
}

void OnSurfaceChangedCB(OH_NativeXComponent* component, void* window)
{
    auto id = WKRuntime::GetXComponentId(component);
    uint64_t width = 0, height = 0;
    OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);

    struct CallbackData
    {
        std::string id;
        void* window;
        uint64_t width;
        uint64_t height;
    }; 

    auto* data = new CallbackData{ id, window, width, height };
    WKRuntime::Invoke(
        [](void* data) {
            auto* callbackData = static_cast<CallbackData*>(data);
            auto* webView = WKRuntime::GetWebView(callbackData->id);
            if (webView != nullptr) {
                webView->OnSurfaceChanged(
                    static_cast<OHNativeWindow*>(callbackData->window),
                    static_cast<int>(callbackData->width),
                    static_cast<int>(callbackData->height)
                );
            }
        },
        data,
        [](void* data) {
            delete static_cast<CallbackData*>(data);
        }
    );
}

void OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window)
{
    auto id = WKRuntime::GetXComponentId(component);

    struct CallbackData
    {
        std::string id;
        void* window;
    };

    auto* data = new CallbackData{ id, window };
    WKRuntime::Invoke(
        [](void* data) {
            auto* callbackData = static_cast<CallbackData*>(data);
            auto* webView = WKRuntime::GetWebView(callbackData->id);
            if (webView != nullptr) {
                webView->OnSurfaceDestroyed(
                    static_cast<OHNativeWindow*>(callbackData->window)
                );
            }
        },
        data,
        [](void* data) {
            delete static_cast<CallbackData*>(data);
        }
    );
}

void DispatchTouchEventCB(OH_NativeXComponent *component, void *window)
{
    auto id = WKRuntime::GetXComponentId(component);

    auto touchEvent = new OH_NativeXComponent_TouchEvent;
    int32_t ret = OH_NativeXComponent_GetTouchEvent(component, window, touchEvent);
    if (ret != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        delete touchEvent;
        return;
    }

    struct CallbackData
    {
        std::string id;
        OH_NativeXComponent_TouchEvent* touchEvent;
    }; 

    auto* data = new CallbackData{ id, touchEvent };
    WKRuntime::Invoke(
        [](void* data) {
            auto* callbackData = static_cast<CallbackData*>(data);
            auto* webView = WKRuntime::GetWebView(callbackData->id);
            if (webView != nullptr) {
                webView->DispatchTouchEvent(callbackData->touchEvent);
            }
        },
        data,
        [](void* data) {
            auto* callbackData = static_cast<CallbackData*>(data);
            delete callbackData->touchEvent;
            delete callbackData;
        }
    );
}
napi_value NapiLoadURL(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_value thisArg;
    void* data;

    if (napi_get_cb_info(env, info, &argc, args, &thisArg, &data) != napi_ok) {
        LOGE("NapiLoadURL: napi_get_cb_info fail");
        return nullptr;
    }
    if (argc < 1) {
        LOGE("NapiLoadURL: invalid number of arguments");
        return nullptr;
    }
    size_t strSize;
    if (napi_get_value_string_utf8(env, args[0], nullptr, 0, &strSize) != napi_ok) {
        LOGE("NapiLoadURL: napi_get_value_string_utf8 fail");
        return nullptr;
    }
    std::string url(strSize + 1, '\0');
    if (napi_get_value_string_utf8(env, args[0], url.data(), strSize + 1, &strSize) != napi_ok) {
        LOGE("NapiLoadURL: napi_get_value_string_utf8 fail");
        return nullptr;
    }
    url.resize(strSize);

    napi_value exportInstance;
    if (napi_get_named_property(env, thisArg, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) != napi_ok) {
        LOGE("InitWPEView: napi_get_named_property fail");
        return nullptr;
    }

    OH_NativeXComponent* nativeXComponent = nullptr;
    if (napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&nativeXComponent)) != napi_ok) {
        LOGE("InitWPEView: napi_unwrap fail");
        return nullptr;
    }

    auto id = WKRuntime::GetXComponentId(nativeXComponent);

    struct CallbackData
    {
        std::string id;
        std::string url;
    };

    auto* callbackData = new CallbackData{ id, url };
    WKRuntime::Invoke(
        [](void* data){
            auto* callbackData = static_cast<CallbackData*>(data);
            auto* webView = WKRuntime::GetWebView(callbackData->id);
            if (webView != nullptr) {
                webView->LoadURL(callbackData->url);
            }
        },
        callbackData,
        [](void* data) {
            delete static_cast<CallbackData*>(data);
        }
    );

    return nullptr;
}

} // namespace

WKWebView::WKWebView(const std::string& id)
    : id_(id)
{
    LOGD("WKWebView::WKWebView id: %{public}s", id.c_str());
}

WKWebView::~WKWebView()
{
    LOGD("WKWebView::~WKWebView id: %{public}s", id_.c_str());

    for (auto handler : signalHandlers_) {
        g_signal_handler_disconnect(webView_, handler);
    }
    signalHandlers_.clear();

    if (webView_ != nullptr) {
        g_object_unref(webView_);
        webView_ = nullptr;
    }

    if (wpeViewRenderer_ != nullptr) {
        wpe_view_ohos_set_renderer(wpeView_, nullptr);
        wpeViewRenderer_->Cleanup();
        wpeViewRenderer_.reset();
    }
}

bool WKWebView::Export(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        {"loadURL", nullptr, NapiLoadURL, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    return true;
}

void WKWebView::RegisterCallbacks(OH_NativeXComponent* component)
{
    callback_.OnSurfaceCreated = OnSurfaceCreatedCB;
    callback_.OnSurfaceChanged = OnSurfaceChangedCB;
    callback_.OnSurfaceDestroyed = OnSurfaceDestroyedCB;
    callback_.DispatchTouchEvent = DispatchTouchEventCB;
    OH_NativeXComponent_RegisterCallback(component, &callback_);
}

void WKWebView::OnSurfaceCreated(OHNativeWindow* window, int width, int height)
{
    nativeWindow_ = window;
    width_ = width;
    height_ = height;

    if (wpeViewRenderer_ == nullptr && wpeView_ != nullptr)
        InitializeRenderer();
}

void WKWebView::OnSurfaceChanged(OHNativeWindow* window, int width, int height)
{
}

void WKWebView::OnSurfaceDestroyed(OHNativeWindow* window)
{
    if (!wpeView_)
      return;
    wpe_view_ohos_set_renderer(wpeView_, nullptr);

    if (wpeViewRenderer_ != nullptr) {
        wpeViewRenderer_->Cleanup();
        wpeViewRenderer_.reset();
    }
}

void WKWebView::DispatchTouchEvent(OH_NativeXComponent_TouchEvent* touchEvent)
{
    wpe_view_ohos_dispatch_touch_event(wpeView_, touchEvent);
}

void WKWebView::Init()
{
    if (webView_ != nullptr) {
        return;
    }

    LOGD("WKWebView::Init");
    webView_ = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW,
        "display", WKRuntime::GetWPEDisplay(),
        nullptr
    ));

    if (webView_ == nullptr) {
        LOGE("Failed to create WebKitWebView");
        return;
    }

    wpeView_ = WPE_VIEW_OHOS(webkit_web_view_get_wpe_view(webView_));
    if (wpeView_ == nullptr) {
        LOGE("Failed to get WPEViewOHOS from WebKitWebView");
        return;
    }

    signalHandlers_.push_back(
        g_signal_connect_swapped(webView_, "load-changed", G_CALLBACK(WKWebView::OnLoadChanged), this));
    signalHandlers_.push_back(
        g_signal_connect_swapped(webView_, "load-failed", G_CALLBACK(WKWebView::OnLoadFailed), this));
    signalHandlers_.push_back(
        g_signal_connect_swapped(webView_, "load-failed-with-tls-errors", G_CALLBACK(WKWebView::OnLoadFailedWithTlsErrors), this));

    auto* network_session = webkit_network_session_get_default();
    auto* data_manager = webkit_network_session_get_website_data_manager(network_session);
    webkit_network_session_set_tls_errors_policy(network_session, WEBKIT_TLS_ERRORS_POLICY_IGNORE);

    auto* settings = webkit_web_view_get_settings(webView_);
    webkit_settings_set_user_agent(settings, "Mozilla/5.0 (Linux; OpenHarmony 6.0) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/60.5 Mobile Safari/605.1.15");

    if (nativeWindow_ != nullptr && wpeViewRenderer_ == nullptr)
        InitializeRenderer();
}

void WKWebView::InitializeRenderer()
{
    LOGD("WKWebView::InitializeRenderer");
    if (nativeWindow_ == nullptr) {
        LOGE("Cannot initialize renderer: nativeWindow_ is nullptr");
        return;
    }
    if (wpeViewRenderer_ != nullptr) {
        LOGD("Renderer already initialized");
        return;
    }

    wpeViewRenderer_ = std::make_shared<WPEViewOHOSGLES3Renderer>();
    if (!wpeViewRenderer_->Initialize(nativeWindow_, width_, height_)) {
        LOGE("Failed to initialize WPEView renderer");
        wpeViewRenderer_ = nullptr;
        return;
    }

    wpe_view_ohos_set_renderer(wpeView_, wpeViewRenderer_);
    wpe_view_ohos_resize(wpeView_, width_, height_);
    wpe_view_map(WPE_VIEW(wpeView_));
}


void WKWebView::LoadURL(const std::string& url)
{
    if (webView_ == nullptr) {
        LOGE("WKWebView::LoadURL - webView_ is nullptr");
        return;
    }
    LOGD("WKWebView::LoadURL - url: %{public}s", url.c_str());
    webkit_web_view_load_uri(webView_, url.c_str());
}

void WKWebView::OnLoadChanged(WKWebView* wkWebView, WebKitLoadEvent loadEvent, WebKitWebView* /*webView*/) noexcept
{
    LOGD("WKWebView::OnLoadChanged - loadEvent: %{public}d", static_cast<int>(loadEvent));
    if (loadEvent == WEBKIT_LOAD_FINISHED) {
        const char* uri = webkit_web_view_get_uri(wkWebView->webView_);
        LOGD("WKWebView::onLoadChanged - Load finished, current URI: %{public}s", uri);
    }
}

int WKWebView::OnLoadFailed(WKWebView* wkWebView, WebKitLoadEvent loadEvent, const char* failingURI, GError* error, WebKitWebView* webView) noexcept
{
    LOGD("WKWebView::OnLoadFailed - loadEvent: %{public}d, failingURI: %{public}s, error: %{public}s", static_cast<int>(loadEvent), failingURI, error->message); 
    return FALSE;
}

int WKWebView::OnLoadFailedWithTlsErrors(WebKitWebView *web_view,
                                                 char                *failing_uri,
                                                 GTlsCertificate     *certificate,
                                                 GTlsCertificateFlags errors,
                                                 void                *user_data) noexcept {
    if (failing_uri == nullptr) {
        LOGE("WKWebView::OnLoadFailedWithTlsErrors - failingURI is nullptr");
        return FALSE;
    }
    LOGD("WKWebView::OnLoadFailedWithTlsErrors - ...");
    return FALSE;
}
