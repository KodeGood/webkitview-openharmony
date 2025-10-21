# WebKitView for OpenHarmony

## Overview

This repository provides a WebView implementation for the OpenHarmony platform using WebKit as the rendering engine.

The project can be built using the public OpenHarmony 6.0 SDK.

**Blog Post (Full Story & Details):**
[WebKit for OpenHarmony](https://medium.com/kodegood/webkit-for-openharmony-3b612c27b958)

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

### 1. Build WPEWebKit for OpenHarmony (arm64)

Clone the repository and execute the following commands:

```bash
git clone https://github.com/KodeGood/webkit-openharmony-cerbero.git
cd webkit-openharmony-cerbero
./cerbero-uninstalled -c config/cross-ohos-arm64.cbc bootstrap
./cerbero-uninstalled -c config/cross-ohos-arm64.cbc package -f wpewebkit
```

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

## Bootstrapping WebKitView with WPEWebKit

WebKitView requires WPEWebKit to be built and installed as a sysroot in your project.

To automate this setup, run:

```bash
python3 tools/bootstrap.py -a arm64 install -c <path-to-your-webkit-openharmony-cerbero>
```

## Building and Running the WebKitView Application

```bash
./hvigorw assembleHap
./install-webkitview
./launch-webkitview
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

