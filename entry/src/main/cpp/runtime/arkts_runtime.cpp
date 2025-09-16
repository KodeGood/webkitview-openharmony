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

#include "arkts_runtime.h"

#include "log.h"

bool ArkTSRuntime::Init(napi_env env)
{
      LOGD("ArkTSRuntime::Init - invoked: tid: %{public}lu",
         static_cast<unsigned long>(pthread_self()));
    return GetInstance().DoInit(env);
}

bool ArkTSRuntime::DoInit(napi_env env)
{
    if (inited_.load(std::memory_order_acquire))
        return true;

    uv_loop_t* loop = nullptr;
    napi_status st = napi_get_uv_event_loop(env, &loop);
    if (st != napi_ok || loop == nullptr)
        return false;

    uvLoop_ = loop;
    arktsThreadId_ = std::this_thread::get_id();

    // Must be called on the thread that owns uvLoop_
    if (uv_async_init(uvLoop_, &async_, &ArkTSRuntime::OnAsync) != 0) {
        uvLoop_ = nullptr;
        return false;
    }
    async_.data = this;

    inited_.store(true, std::memory_order_release);
    return true;
}

void ArkTSRuntime::OnAsync(uv_async_t* handle)
{
    auto* self = static_cast<ArkTSRuntime*>(handle->data);

    LOGD("ArkTSRuntime::OnAsync - invoked: tid: %{public}lu",
         static_cast<unsigned long>(pthread_self()));
    std::deque<std::function<void()>> local;
    {
      std::lock_guard<std::mutex> lock(self->mutex_);
      local.swap(self->deque_);
    }

    for (auto& fn : local)
      if (fn) fn();  // Runs on ArkTS/libuv thread
}
