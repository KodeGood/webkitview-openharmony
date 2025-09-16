# Copyright (C) 2025 Jani Hautakangas <jani@kodegood.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

cmake_minimum_required(VERSION 3.5.0)

# =============================================================================
# OpenHarmony (OHOS) + external WebKit SDK toolchain wrapper
# Expected layout (YOU confirmed):
#   .webkit/<version>/<abi>/sdk
#   .webkit/<version>/<abi>/runtime
#
# You can override via:
#   -DWEBKIT_BASE_DIR=/abs/path/to/.webkit
#   -DWEBKIT_VERSION=current     # or 2.50.0
#   -DWEBKIT_ABI=arm64-v8a       # default from OHOS toolchain
# =============================================================================

# --- Locate and include the OHOS toolchain relative to the SDK CMake binary ---
# Assumes:
#   CMAKE_COMMAND = .../<api>/native/build-tools/cmake/bin/cmake
#   toolchain     = .../<api>/native/build/cmake/ohos.toolchain.cmake
get_filename_component(_CMAKE_BIN_DIR "${CMAKE_COMMAND}" DIRECTORY)              # .../cmake/bin
get_filename_component(_BUILD_TOOLS_DIR "${_CMAKE_BIN_DIR}/.." ABSOLUTE)         # .../build-tools/cmake
get_filename_component(_NATIVE_DIR      "${_BUILD_TOOLS_DIR}/../.." ABSOLUTE)    # .../native
set(_OHOS_TOOLCHAIN "${_NATIVE_DIR}/build/cmake/ohos.toolchain.cmake")

if(NOT EXISTS "${_OHOS_TOOLCHAIN}")
  message(FATAL_ERROR
    "Cannot find ohos.toolchain.cmake.\n"
    "CMAKE_COMMAND: ${CMAKE_COMMAND}\n"
    "Tried:        ${_OHOS_TOOLCHAIN}\n"
    "Check that your OHOS SDK keeps the standard layout.")
endif()

include("${_OHOS_TOOLCHAIN}")  # sets CMAKE_SYSROOT, compilers, OHOS_ARCH, etc.

# --- Inputs -------------------------------------------------------------------
# Version label (folder under .webkit)
set(WEBKIT_VERSION "${WEBKIT_VERSION}" CACHE STRING
    "WebKit version under .webkit/<version>/<abi> (e.g., 2.50.0 or current)")
if(NOT WEBKIT_VERSION)
  set(WEBKIT_VERSION "current")
endif()

# ABI (default from OHOS toolchain’s OHOS_ARCH)
set(WEBKIT_ABI "${WEBKIT_ABI}" CACHE STRING "ABI (arm64-v8a, armeabi-v7a, x86_64)")
if(NOT WEBKIT_ABI)
  if(DEFINED OHOS_ARCH AND NOT OHOS_ARCH STREQUAL "")
    set(WEBKIT_ABI "${OHOS_ARCH}")
  else()
    message(FATAL_ERROR "Cannot determine ABI. Pass -DWEBKIT_ABI=arm64-v8a|armeabi-v7a|x86_64")
  endif()
endif()

# Base .webkit directory:
# If not provided, search upwards from this file for a folder named ".webkit"
set(WEBKIT_BASE_DIR "${WEBKIT_BASE_DIR}" CACHE PATH "Base .webkit directory")
if(NOT WEBKIT_BASE_DIR)
  set(_start_dir "${CMAKE_CURRENT_LIST_DIR}")
  set(_found FALSE)
  foreach(_i RANGE 0 10)  # walk up to 10 levels
    if(EXISTS "${_start_dir}/.webkit")
      set(WEBKIT_BASE_DIR "${_start_dir}/.webkit")
      set(_found TRUE)
      break()
    endif()
    get_filename_component(_start_dir "${_start_dir}/.." ABSOLUTE)
  endforeach()
  if(NOT _found)
    # Fallback: assume repo root has .webkit next to entry/
    set(WEBKIT_BASE_DIR "${CMAKE_SOURCE_DIR}/.webkit")
  endif()
endif()
file(TO_CMAKE_PATH "${WEBKIT_BASE_DIR}" WEBKIT_BASE_DIR)

# --- Layout mapping (YOUR layout) ---------------------------------------------
# .webkit/<version>/<abi>/{sdk,runtime}
set(_WEBKIT_VER_DIR       "${WEBKIT_BASE_DIR}/${WEBKIT_VERSION}")
set(WEBKIT_SDK_PREFIX     "${_WEBKIT_VER_DIR}/${WEBKIT_ABI}/sdk")
set(WEBKIT_RUNTIME_PREFIX "${_WEBKIT_VER_DIR}/${WEBKIT_ABI}/runtime")  # informational

if(NOT EXISTS "${WEBKIT_SDK_PREFIX}")
  message(FATAL_ERROR
    "WebKit SDK not found at:\n  ${WEBKIT_SDK_PREFIX}\n"
    "Expected layout: .webkit/<version>/<abi>/{sdk,runtime}\n"
    "Hint: run your bootstrap to populate it, or set WEBKIT_BASE_DIR/WEBKIT_VERSION/WEBKIT_ABI.\n"
    "Resolved WEBKIT_BASE_DIR = ${WEBKIT_BASE_DIR}")
endif()

# --- Make the SDK discoverable to CMake find_* and packages -------------------
# Keep OHOS sysroot as the true sysroot; add WebKit SDK as extra roots.
# Prepend so WebKit’s headers/libs are preferred for its packages.
list(PREPEND CMAKE_FIND_ROOT_PATH
  "${WEBKIT_SDK_PREFIX}"
  "${WEBKIT_SDK_PREFIX}/usr"
)
list(PREPEND CMAKE_PREFIX_PATH
  "${WEBKIT_SDK_PREFIX}"
  "${WEBKIT_SDK_PREFIX}/usr"
)

# Cross-compile find behavior
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# --- pkg-config isolation to the WebKit SDK -----------------------------------
# Prefer the SDK's pkg-config binary if it exists
if(EXISTS "${WEBKIT_SDK_PREFIX}/bin/pkg-config")
  set(ENV{PKG_CONFIG} "${WEBKIT_SDK_PREFIX}/bin/pkg-config")
elseif(EXISTS "${WEBKIT_SDK_PREFIX}/usr/bin/pkg-config")
  set(ENV{PKG_CONFIG} "${WEBKIT_SDK_PREFIX}/usr/bin/pkg-config")
endif()

# Build a strict PKG_CONFIG_LIBDIR from SDK .pc directories
set(_WK_PC_DIRS
  "${WEBKIT_SDK_PREFIX}/lib/pkgconfig"
  "${WEBKIT_SDK_PREFIX}/usr/lib/pkgconfig"
  "${WEBKIT_SDK_PREFIX}/share/pkgconfig"
  "${WEBKIT_SDK_PREFIX}/usr/share/pkgconfig"
)
set(_PC_LIBDIRS "")
foreach(_pcdir IN LISTS _WK_PC_DIRS)
  if(EXISTS "${_pcdir}")
    list(APPEND _PC_LIBDIRS "${_pcdir}")
  endif()
endforeach()
if(_PC_LIBDIRS)
  list(JOIN _PC_LIBDIRS ":" _PC_LIBDIRS_JOINED)
  set(ENV{PKG_CONFIG_LIBDIR} "${_PC_LIBDIRS_JOINED}")
endif()

# IMPORTANT:
#  - Do NOT set PKG_CONFIG_SYSROOT_DIR; the WebKit SDK lives outside the OHOS sysroot.
#  - Clear PKG_CONFIG_PATH to avoid host pollution.
unset(ENV{PKG_CONFIG_SYSROOT_DIR})
unset(ENV{PKG_CONFIG_PATH})

# --- Diagnostics ---------------------------------------------------------------
message(STATUS "OHOS toolchain     : ${_OHOS_TOOLCHAIN}")
message(STATUS "ABI (WEBKIT_ABI)   : ${WEBKIT_ABI}")
message(STATUS "WebKit base dir    : ${WEBKIT_BASE_DIR}")
message(STATUS "WebKit version dir : ${_WEBKIT_VER_DIR}")
message(STATUS "WebKit SDK prefix  : ${WEBKIT_SDK_PREFIX}")
if(EXISTS "${WEBKIT_RUNTIME_PREFIX}")
  message(STATUS "WebKit Runtime     : ${WEBKIT_RUNTIME_PREFIX} (not used by CMake; packaged via symlinks)")
endif()

# (Optional) debug pkg-config output
# find_package(PkgConfig REQUIRED)
# execute_process(COMMAND "$ENV{PKG_CONFIG}" --cflags wpe-webkit-2.0
#                 OUTPUT_VARIABLE _pcflags OUTPUT_STRIP_TRAILING_WHITESPACE
#                 ERROR_QUIET)
# if(_pcflags)
#   message(STATUS "pkg-config cflags (wpe-webkit-2.0): ${_pcflags}")
# endif()

