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

#include "environment.h"

namespace Environment {

void Initialize(const std::vector<std::string>& params)
{
    // params layout:
    // 0 - process type
    // 1 - cache dir
    // 2 - files dir 
    // 3 - temp dir
    // 4 - bundle code dir

    // cache path
    setenv("TMP", params[1].c_str(), 1);
    setenv("TEMP", params[1].c_str(), 1);
    setenv("TMPDIR", params[1].c_str(), 1);
    setenv("XDG_CACHE_HOME", params[1].c_str(), 1);
    setenv("XDG_RUNTIME_DIR", params[1].c_str(), 1);

    // files path
    setenv("FONTCONFIG_PATH", params[2].c_str(), 1);
    setenv("HOME", params[2].c_str(), 1);
    setenv("XDG_DATA_HOME", params[2].c_str(), 1);
    setenv("XDG_DATA_DIRS", params[2].c_str(), 1);
    setenv("XDG_CONFIG_HOME", params[2].c_str(), 1);
    setenv("XDG_CONFIG_DIRS", params[2].c_str(), 1);

    std::string gioModulesDir = params[4] + "/libs/arm64/gio/modules/";
    setenv("GIO_EXTRA_MODULES", gioModulesDir.c_str(), 1);

    std::string injectedBundleDir = params[4] + "/libs/arm64/wpe-webkit-2.0/injected-bundle/";
    setenv("WEBKIT_INJECTED_BUNDLE_PATH", injectedBundleDir.c_str(), 1);
}

} // namespace Environment

