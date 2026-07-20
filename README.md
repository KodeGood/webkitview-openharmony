# WebKitView for OpenHarmony

## Overview

This repository provides a WebView implementation for the OpenHarmony platform using WebKit as the rendering engine.

The project can be built using the public OpenHarmony 6.0 SDK.

**Blog Posts (Full Story & Details):**

- [WebKit for OpenHarmony](https://kodegood.com/blog/webkit-for-openharmony/)
- [Building WebKit for OpenHarmony with vcpkg](https://kodegood.com/blog/building-webkit-for-openharmony-with-vcpkg/)

**Note:** This project is in an early development stage. Many features are not yet implemented, and various issues are present. Please refer to the **Known Issues** section at the end of this document.

---

## Requirements

- Ubuntu 24.04 or later (or compatible Linux distribution)
- OpenHarmony SDK
- HiHope HH-SCDAYU200 development board (or compatible device)
- OpenHarmony 6.0 arm64 image flashed on the device
- `python3`
- `npm`
- `nodejs`

**Note:** Root access to the development board is required.

## Setting Up Your Development Board

### 1. Download (or Build) and Flash OpenHarmony 6.0 Image

The DAYU200 arm64 image is available from the [OpenHarmony Daily Build](https://ci.openharmony.cn/workbench/cicd/dailybuild/dailylist).

To find the correct image, use the following filters:
- **Project:** openharmony  
- **Branch:** OpenHarmony-6.0-Release  
- **Time Range:** Current Month  
- **Status:** Running Build  

Alternatively, you can build the image yourself.

> **TODO:** Add instructions to build the OpenHarmony image using the [Docker build environment](https://github.com/KodeGood/docker-openharmony).

---

### 2. Prepare the Development Board

Several configuration changes are required for WebKitView to run properly on an OpenHarmony device:

- By default, the OpenHarmony runtime does not allow third-party applications to spawn child processes. This restriction must be lifted.
- The OpenHarmony memory manager may kill applications that consume more memory than the default limit. WebKitView requires a higher memory limit.
- The OpenHarmony public SDK does not include an API for GPU buffer sharing via a plain domain socket file descriptor. This functionality is provided by a custom library: [`native_buffer_socket`](https://github.com/KodeGood/graphic_surface-openharmony/tree/OpenHarmony-6.0-KodeGood), included as a prebuilt binary at `prebuilts/libnative_buffer_socket.z.so`.

To apply all these modifications, connect the device to your PC and run

```bash
tools/prepare-board.sh
```

## Setting Up Your Environment

### 1. Build WebKit for OpenHarmony (arm64)

WebKit and its full dependency tree are built with [vcpkg](https://github.com/microsoft/vcpkg).
Clone the build repository and execute the following commands:

```bash
git clone https://github.com/KodeGood/webkit-openharmony-vcpkg.git
cd webkit-openharmony-vcpkg
export OHOS_SDK_ROOT=/path/to/ohos/sdk/<api>   # dir with native/build/cmake/ohos.toolchain.cmake
export VCPKG_ROOT=$PWD/build/vcpkg
python3 scripts/bootstrap_vcpkg.py             # clone + bootstrap vcpkg at the pinned baseline
scripts/build.sh                               # vcpkg install (deps + webkit) then package -> dist/
```

Artifacts land in `dist/`. See the build repository's
[`README.md`](https://github.com/KodeGood/webkit-openharmony-vcpkg) for prerequisites and details.

WebKit patches are available at:

[KodeGood/WebKit/wpewebkit-2.50.0-ohos](https://github.com/KodeGood/WebKit/tree/wpewebkit-2.50.0-ohos)

### 2. Install OpenHarmony SDK

Download the OpenHarmony 6.0 SDK from the official OpenHarmony website. See [OpenHarmony-v6.0-release.md](https://gitee.com/openharmony/docs/blob/master/en/release-notes/OpenHarmony-v6.0-release.md) for more details.

Install the SDK using the provided script:

```bash
tools/install-ohos-sdk.sh <openharmony-sdk-package>/ohos-sdk-windows_linux-public.tar.gz linux <target-directory>
```

Add the following environment variables to your shell profile (e.g., `~/.bashrc` or `~/.zshrc`):

```bash
export OHOS_BASE_SDK_HOME=<path-to-your-openharmony-sdk>
export OHOS_SDK_NATIVE=${OHOS_BASE_SDK_HOME}/20/native
export OHOS_SDK_TOOLCHAINS=${OHOS_BASE_SDK_HOME}/20/toolchains
export PATH=$PATH:$OHOS_SDK_TOOLCHAINS:$OHOS_SDK_NATIVE/llvm/bin
```

## Bootstrapping WebKitView with WebKit

WebKitView requires WebKit to be built and installed as a sysroot in your project.

To automate this setup, run:

```bash
python3 tools/bootstrap.py -a arm64 install --vcpkg <path-to-your-webkit-openharmony-vcpkg>/dist
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
- SSL websites are not supported yet.  
- Video playback is not yet implemented.  
- WebGL is not supported.  
- IME (Input Method Editor) is not integrated.  
- Performance is not yet optimized.  
- High memory usage may cause the system to terminate the app.  
- Scrolling performance is not smooth.  
- Several features remain unimplemented.

