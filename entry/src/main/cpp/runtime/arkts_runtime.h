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

#include <napi/native_api.h>
#include <uv.h>
#include <mutex>
#include <deque>
#include <functional>
#include <atomic>
#include <future>
#include <thread>
#include <type_traits>

class ArkTSRuntime {

public:
    static bool Init(napi_env env);

    template<class F>
    static void Invoke(F&& f) {
        GetInstance().DoInvoke(std::forward<F>(f));
    }

    template<class F>
    static auto InvokeSync(F&& f) -> decltype(f()) {
        return GetInstance().DoInvokeSync(std::forward<F>(f));
    }

    static bool IsOnArkTSThread() {
        return GetInstance().arktsThreadId_ == std::this_thread::get_id();
    }

private:

    ArkTSRuntime() = default;
    ~ArkTSRuntime() = default;

    bool DoInit(napi_env env);

    template<class F>
    void DoInvoke(F&& f) {
        if (!inited_.load(std::memory_order_acquire)) return;

        // Wrap possibly move-only F in a shared_ptr
        using Fn = std::decay_t<F>;
        auto task = std::make_shared<Fn>(std::forward<F>(f));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Store a copyable trampoline (captures only a shared_ptr)
            deque_.emplace_back([task]() { (*task)(); });
        }
        uv_async_send(&async_);
    }

    template<class F>
    auto DoInvokeSync(F&& f) -> decltype(f()) {
        using R = decltype(f());

        if (IsOnArkTSThread()) {
            if constexpr (std::is_void_v<R>) { f(); return; }
            else { return f(); }
        }

        // Promise also wrapped in shared_ptr to keep the trampoline copyable
        auto prm = std::make_shared<std::promise<R>>();
        auto fut = prm->get_future();

        using Fn = std::decay_t<F>;
        auto task = std::make_shared<Fn>(std::forward<F>(f));

        DoInvoke([task, prm]() mutable {
            if constexpr (std::is_void_v<R>) {
                (*task)();
                prm->set_value();
            } else {
                prm->set_value((*task)());
            }
        });

        if constexpr (std::is_void_v<R>) {
            fut.get();
        } else {
            return fut.get();
        }
    }

    static ArkTSRuntime& GetInstance() noexcept
    {
        static ArkTSRuntime s_singleton;
        return s_singleton;
    }

    static void OnAsync(uv_async_t* handle);

    std::atomic_bool inited_ { false };
    uv_loop_t* uvLoop_ { nullptr };
    uv_async_t async_ {};
    std::mutex mutex_;
    std::deque<std::function<void()>> deque_;
    std::thread::id arktsThreadId_{};
};

