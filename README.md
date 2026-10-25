# WebKitView for OpenHarmony

## Overview

This repository provides a WebView implementation for the OpenHarmony platform using WebKit as the rendering engine.

The project can be built using the public OpenHarmony 6.1 SDK (API level 23).

**Blog Posts (Full Story & Details):**

- [WebKit for OpenHarmony](https://kodegood.com/blog/webkit-for-openharmony/)
- [Building WebKit for OpenHarmony with vcpkg](https://kodegood.com/blog/building-webkit-for-openharmony-with-vcpkg/)

**Note:** This project is in an early development stage. Many features are not yet implemented, and various issues are present. Please refer to the **Known Issues** section at the end of this document.

---

## Requirements

- Ubuntu 24.04 or later (or compatible Linux distribution)
- OpenHarmony 6.1 SDK (API level 23)
- HiHope HH-SCDAYU200 development board (or compatible device)
- OpenHarmony 6.1 arm64 image flashed on the device
- `python3`
- `npm`
- `nodejs`

**Note:** Root access to the development board is required.

## Setting Up Your Development Board

### 1. Download (or Build) and Flash OpenHarmony 6.1 Image

The DAYU200 arm64 image is available from the [OpenHarmony Daily Build](https://ci.openharmony.cn/workbench/cicd/dailybuild/dailylist).

To find the correct image, use the following filters:
- **Project:** openharmony  
- **Branch:** OpenHarmony-6.1-Release  
- **Time Range:** Current Month  
- **Status:** Running Build  

Alternatively, you can build the image yourself.

> **TODO:** Add instructions to build the OpenHarmony image using the [Docker build environment](https://github.com/KodeGood/docker-openharmony).

---

### 2. Prepare the Development Board

Several configuration changes are required for WebKitView to run properly on an OpenHarmony device:

- By default, the OpenHarmony runtime does not allow third-party applications to spawn native child processes. WebKit runs its web and network processes as native child processes, so this restriction must be lifted for the app, and the per-app child process limit raised.
- The OpenHarmony memory manager may kill applications that consume more memory than the default limit. WebKitView requires a higher memory limit.

WebKit's inter-process communication, including GPU buffer sharing, uses the public binder IPC API
(IPCKit), so no custom system libraries are needed on the device.

To apply all these modifications, connect the device to your PC and run

```bash
tools/prepare-board.sh
```

## Setting Up Your Environment

### 1. Install OpenHarmony SDK

Download the OpenHarmony 6.1 SDK (API level 23) from the official OpenHarmony website. See the
[OpenHarmony release notes](https://gitee.com/openharmony/docs/tree/master/en/release-notes) for more details.
The project is developed with SDK 6.1.0.31 (Release).

Install the SDK using the provided script:

```bash
tools/install-ohos-sdk.sh <openharmony-sdk-package>/ohos-sdk-windows_linux-public.tar.gz linux <target-directory>
```

Add the following environment variables to your shell profile (e.g., `~/.bashrc` or `~/.zshrc`):

```bash
export OHOS_BASE_SDK_HOME=<path-to-your-openharmony-sdk>
export OHOS_SDK_NATIVE=${OHOS_BASE_SDK_HOME}/23/native
export OHOS_SDK_TOOLCHAINS=${OHOS_BASE_SDK_HOME}/23/toolchains
export PATH=$PATH:$OHOS_SDK_TOOLCHAINS:$OHOS_SDK_NATIVE/llvm/bin
```

### 2. Build WebKit for OpenHarmony (arm64)

WebKit is built from the [KodeGood/WebKit](https://github.com/KodeGood/WebKit) fork, which carries
the OpenHarmony port. The build runs in the WebKit tree, like the Windows port: [vcpkg](https://github.com/microsoft/vcpkg)
provides the dependencies (bootstrapped automatically at a pinned baseline), and WebKit itself is a
regular CMake/Ninja build.

```bash
git clone --depth 1 -b wpewebkit-2.54.2-ohos https://github.com/KodeGood/WebKit.git
cd WebKit
export OHOS_SDK_ROOT=${OHOS_BASE_SDK_HOME}/23   # dir with native/build/cmake/ohos.toolchain.cmake
Tools/Scripts/build-webkit-ohos --release
```

The first build also builds the dependencies and takes a while; later builds reuse vcpkg's binary
cache, and `ninja -C WebKitBuild/OHOS/Release` rebuilds WebKit incrementally. The packaged result
lands in `WebKitBuild/OHOS/Release/dist`. See
[`WebKitLibraries/ohos/README.md`](https://github.com/KodeGood/WebKit/blob/wpewebkit-2.54.2-ohos/WebKitLibraries/ohos/README.md)
for host prerequisites and build options.

WebKitView builds against the
[`wpewebkit-2.54.2-ohos`](https://github.com/KodeGood/WebKit/tree/wpewebkit-2.54.2-ohos) branch
(WPE WebKit 2.54.2 with the OpenHarmony patches); older branches are not supported.

## Bootstrapping WebKitView with WebKit

WebKitView requires WebKit to be built and installed as a sysroot in your project.

To automate this setup, run:

```bash
python3 tools/bootstrap.py -a arm64 install --vcpkg <path-to-your-webkit>/WebKitBuild/OHOS/Release/dist
```

## Building and Running the WebKitView Application

```bash
./hvigorw assembleHap
./install-webkitview
./launch-webkitview
```

### Troubleshooting: `code:9568404 delivery sign profile failed` (WIP Volla X23 + Oniro)

On the work-in-progress Volla X23 + Oniro image, `install-webkitview` fails at the
install step with `code:9568404 error: delivery sign profile failed.` This board's
Halium kernel has no `/dev/code_sign` node (the OHOS `code_sign` driver isn't ported),
but the userspace still expects it for **debug** profiles. Sign with a **release**
profile, which skips that path:

```bash
PROFILE_KIND=release ./tools/gen-signing-config.sh com.kodegood.webkitview \
  .autosign-release "$OHOS_SDK_TOOLCHAINS"
AUTOSIGN_DIR=.autosign-release ./install-webkitview
```

## Known Issues

- First launch may take longer than expected — please be patient.  
- Launching may fail occasionally; retry if necessary.  
- Random crashes may occur; restarting the app typically resolves them.  
- Many websites crash.  
- Only websites included in the demo app bookmark list work reliably.  
- Video playback works (e.g. YouTube), but some GStreamer plugins fail to load because their helper libraries are not packaged yet; this includes the MP4 demuxer, so MP4 media may not play.  
- IME works (typing, composition, Enter, caret movement, select-all), but cut/copy/paste don't work yet (no clipboard backend) and the candidate window may be placed incorrectly.  
- WebGL is not supported.  
- Performance is not yet optimized.  
- High memory usage may cause the system to terminate the app. WebKit's memory pressure monitor is disabled on OpenHarmony, so WebKit doesn't react to low memory.  
- Scrolling performance is not smooth.  
- Several features remain unimplemented.
