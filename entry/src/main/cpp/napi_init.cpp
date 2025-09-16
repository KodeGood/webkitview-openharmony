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

#include <epoxy/egl.h>

#include "common/log.h"
#include "runtime/arkts_runtime.h"
#include "runtime/wk_runtime.h"
#include "runtime/wk_web_view.h"

#ifdef __cplusplus
extern "C" {
#endif


static napi_value NapiInit(napi_env env, napi_callback_info info)
{
    LOGD("Init");
    if ((env == nullptr) || (info == nullptr)) {
        LOGE("Init: env or info is null");
        return nullptr;
    }

    napi_value thisArg;
    if (napi_get_cb_info(env, info, nullptr, nullptr, &thisArg, nullptr) != napi_ok) {
        LOGE("Init: napi_get_cb_info fail"); 
        return nullptr;
    }

    napi_value exportInstance;
    if (napi_get_named_property(env, thisArg, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) != napi_ok) {
        LOGE("Init: napi_get_named_property fail");
        return nullptr;
    }

    OH_NativeXComponent* nativeXComponent = nullptr;
    if (napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&nativeXComponent)) != napi_ok) {
        LOGE("Init: napi_unwrap fail");
        return nullptr;
    }

    auto id = WKRuntime::GetXComponentId(nativeXComponent);
    WKRuntime::RequestWebViewInit(id);

    return nullptr;
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    LOGE("Init");

    napi_property_descriptor desc[] = {
        {"init", nullptr, NapiInit, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    bool ret = ArkTSRuntime::Init(env);
    ret &= WKRuntime::Export(env, exports);
    ret &= WKWebView::Export(env, exports);
    if (!ret) {
        LOGE("Init failed");
    }

    return exports;
}
EXTERN_C_END

static napi_module webkitViewModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "webkitview",
    .nm_priv = ((void*)0),
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterModule(void)
{
    napi_module_register(&webkitViewModule);
}

#ifdef __cplusplus
}
#endif
