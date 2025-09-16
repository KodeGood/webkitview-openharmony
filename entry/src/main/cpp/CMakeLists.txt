cmake_minimum_required(VERSION 3.16.0)
project(webkitview-openharmony LANGUAGES C CXX)

project(webkitview-openharmony)

set(WEBKIT_VIEW_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${WEBKIT_VIEW_ROOT_PATH}
                    ${WEBKIT_VIEW_ROOT_PATH}/common)

# ---- OHOS SDK libs ----
find_library(HILOG_LIB NAMES hilog_ndk.z REQUIRED)
find_library(NAPI_LIB  NAMES ace_napi.z  REQUIRED)
find_library(ACE_LIB   NAMES ace_ndk.z   REQUIRED)
find_library(IPC_LIB   NAMES ipc_capi REQUIRED)
find_library(CHILDPROC_LIB NAMES child_process REQUIRED)
find_library(NATIVE_BUFFER_LIB NAMES native_buffer REQUIRED)
find_library(NATIVE_WINDOW_LIB NAMES native_window REQUIRED)
find_library(EGL_LIB  NAMES EGL REQUIRED)
find_library(GLES3_LIB NAMES GLESv3 REQUIRED)
find_library(ABILITY_RUNTIME_LIB NAMES ability_runtime REQUIRED)
find_library(UV_LIB NAMES uv REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(WEBKIT REQUIRED IMPORTED_TARGET wpe-webkit-2.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET gobject-2.0 glib-2.0 gio-2.0)

# ---- webkitview ----
add_library(webkitview SHARED
  common/environment.cpp
  napi_init.cpp
  platform/gles3/wpe_view_ohos_gles3_renderer.cpp
  platform/wpe_display_ohos.cpp
  platform/wpe_toplevel_ohos.cpp
  platform/wpe_view_ohos.cpp
  runtime/arkts_runtime.cpp
  runtime/wk_runtime.cpp
  runtime/wk_web_view.cpp
)

target_include_directories(webkitview PRIVATE
  ${WPEVIEW_ROOT_PATH}
  ${WPEVIEW_ROOT_PATH}/common
)

target_link_libraries(webkitview
  PRIVATE
    PkgConfig::WEBKIT
    PkgConfig::GLIB
    ${HILOG_LIB}
    ${ACE_LIB}
    ${NAPI_LIB}
    ${IPC_LIB}
    ${CHILDPROC_LIB}
    ${NATIVE_BUFFER_LIB}
    ${NATIVE_WINDOW_LIB}
    ${EGL_LIB}
    ${GLES3_LIB}
    ${ABILITY_RUNTIME_LIB}
    ${UV_LIB}
)

# ---- webkit_web_process ----
add_library(webkit_web_process SHARED
  common/environment.cpp
  web_process/web_process.cpp
)

target_include_directories(webkit_web_process PRIVATE
  ${WPEVIEW_ROOT_PATH}
  ${WPEVIEW_ROOT_PATH}/common
)

target_link_libraries(webkit_web_process
  PRIVATE
    PkgConfig::WEBKIT
    PkgConfig::GLIB
    ${HILOG_LIB}
    ${CHILDPROC_LIB}
)

# ---- webkit_network_process ----
add_library(webkit_network_process SHARED
  common/environment.cpp
  network_process/network_process.cpp
)

target_include_directories(webkit_network_process PRIVATE
  ${WPEVIEW_ROOT_PATH}
  ${WPEVIEW_ROOT_PATH}/common
)

target_link_libraries(webkit_network_process
  PRIVATE
    PkgConfig::WEBKIT
    PkgConfig::GLIB
    ${HILOG_LIB}
    ${CHILDPROC_LIB}
)

target_compile_features(webkitview PRIVATE cxx_std_17)
target_compile_features(webkit_network_process PRIVATE cxx_std_17)
target_compile_features(webkit_web_process PRIVATE cxx_std_17)

file(GLOB RUNTIME_LIBS "${CMAKE_SOURCE_DIR}/../../../../.webkit/current/${OHOS_ARCH}/runtime/lib/*.so")
file(COPY ${RUNTIME_LIBS} DESTINATION "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/")

file(GLOB WEBKIT_INJECTED_BUNDLE "${CMAKE_SOURCE_DIR}/../../../../.webkit/current/${OHOS_ARCH}/runtime/lib/gio/modules/*.so")
file(COPY ${WEBKIT_INJECTED_BUNDLE} DESTINATION "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/gio/modules/")

file(GLOB WEBKIT_INJECTED_BUNDLE "${CMAKE_SOURCE_DIR}/../../../../.webkit/current/${OHOS_ARCH}/runtime/lib/wpe-webkit-2.0/injected-bundle/*.so")
file(COPY ${WEBKIT_INJECTED_BUNDLE} DESTINATION "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/wpe-webkit-2.0/injected-bundle/")
