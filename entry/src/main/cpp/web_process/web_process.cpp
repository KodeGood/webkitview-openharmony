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


#include <AbilityKit/native_child_process.h>

#include <cinttypes>
#include <dlfcn.h>
#include <string>
#include <vector>

#include "environment.h"
#include "log.h"

namespace {

// Parse colon-separated list into vector<string>. Allows empty segments.
int SplitByColon(const char* str, std::vector<std::string>& out)
{
    out.clear();
    if (str == nullptr) {
        return -1;
    }

    const char* start = str;
    const char* end = str;
    while (*end != '\0') {
        if (*end == ':') {
            out.emplace_back(start, end - start);
            start = end + 1;
        }
        end++;
    }
    if (start != end) {
        out.emplace_back(start, end - start);
    }
    return static_cast<int>(out.size());
}

} // namespace

#ifdef __cplusplus
extern "C" {

void Main(NativeChildProcess_Args args)
{
    LOGD("libwebprocess::Main");

    std::vector<std::string> params;
    int err = SplitByColon(args.entryParams, params);
    if (err < 1 || params.size() < 5) {
        LOGE("libwebprocess::Main - invalid entryParams: %{public}s", args.entryParams);
        return;
    }

    Environment::Initialize(params);

    setenv("WEBKIT_SKIA_ENABLE_CPU_RENDERING", "1", 1);
    setenv("WEBKIT_SKIA_GPU_PAINTING_THREADS", "0", 1);

    auto handle = dlopen("libWPEWebKit-2.0.so", RTLD_LAZY | RTLD_GLOBAL);
    if (handle == nullptr) {
        LOGE("Failed to load libWPEWebKit-2.0.so: %{public}s", dlerror());
        return;
    }

    NativeChildProcess_Fd *fdArgs = args.fdList.head;
    if (fdArgs != nullptr) {
        // Mangled C++ symbol for WebKit::WebProcessMain(int, char**)
        static constexpr const char* const entrypointName =
            "_ZN6WebKit14WebProcessMainEiPPc";

        using ProcessEntryPoint = int(int, char**);
        auto* entrypoint = reinterpret_cast<ProcessEntryPoint*>(
            dlsym(handle, entrypointName)
        );

        LOGD("libWebProcess::Main - %{public}s, fd: %{public}d, entryPoint: %{public}p",
             args.entryParams, fdArgs->fd, entrypoint);

        static constexpr size_t NUMBER_BUFFER_SIZE = 32;
        char socketFd[NUMBER_BUFFER_SIZE];

        (void)snprintf(socketFd, NUMBER_BUFFER_SIZE, "%d", fdArgs->fd);

        const int numArgs = 3;
        auto argv = new char*[numArgs];
        argv[0] = strdup(params[0].c_str());
        argv[1] = fdArgs->fdName;
        argv[2] = socketFd;

        LOGD("libwebprocess::Main - entrypoint start");
        (*entrypoint)(numArgs, argv);
        LOGD("libwebprocess::Main - entrypoint end");

        delete[] argv;
    }
    dlclose(handle);
    LOGD("libwebprocess::Main - end");
}

} // extern "C"
#endif
