#!/usr/bin/env python3
# -*- coding: utf-8 -*-

##
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
##

import argparse
import os
import shutil
import sys
from pathlib import Path

# ------------------------------ ABI handling ----------------------------------

ABI_MAP = {
    "arm64": "arm64-v8a",
    "aarch64": "arm64-v8a",
    "x86_64": "x86_64",
    "amd64": "x86_64",
}

# ------------------------------- Bootstrap ------------------------------------

class Bootstrap:
    default_arch = "arm64"      # input name (mapped via ABI_MAP)
    default_version = "2.54.2"

    def __init__(self, args=None):
        args = vars(args) if args and not isinstance(args, dict) else (args or {})

        in_arch = args.get("arch", self.default_arch)
        self._abi = ABI_MAP.get(in_arch, in_arch)
        self._version = args.get("version", self.default_version)
        # The dist/ tree of a WebKit build (WebKitBuild/OHOS/Release/dist), which also holds the
        # vcpkg-built dependencies.
        self._dist_path = args.get("dist")
        if self._dist_path:
            self._dist_path = os.path.realpath(self._dist_path)

        self._project_root = Path(__file__).resolve().parents[1]

        # Final layout:
        # .webkit/<version>/<abi>/sdk
        # .webkit/<version>/<abi>/runtime
        # .webkit/current -> <version>
        self._webkit_base = self._project_root / ".webkit"
        self._version_dir = self._webkit_base / self._version
        self._abi_dir_versioned = self._version_dir / self._abi
        self._sdk_dir_versioned = self._abi_dir_versioned / "sdk"
        self._runtime_dir_versioned = self._abi_dir_versioned / "runtime"
        self._current_link = self._webkit_base / "current"

        # entry/libs/<abi> link
        self._entry_dir = self._project_root / "entry"
        self._entry_libs_abi = self._entry_dir / "libs" / self._abi

    # ------------------------ helpers -------------------------

    def _update_current(self, alias_path: Path, target_dir: Path):
        """
        Make '.webkit/current' point to the given version directory.
        Prefer a relative symlink; fall back to copy when symlink is unavailable.
        """
        alias_path.parent.mkdir(parents=True, exist_ok=True)
        if alias_path.exists() or alias_path.is_symlink():
            if alias_path.is_dir() and not alias_path.is_symlink():
                shutil.rmtree(alias_path)
            else:
                alias_path.unlink(missing_ok=True)
        try:
            alias_path.symlink_to(target_dir.name)  # relative symlink: current -> <version>
        except (OSError, NotImplementedError):
            shutil.copytree(target_dir, alias_path)

    def _symbolize_file_path(self) -> Path:
        """Return the <project>/.symbolize-lib-paths file path."""
        return self._project_root / ".symbolize-lib-paths"

    def _write_symbolize_lib_paths(self):
        """
        Write <project>/.symbolize-lib-paths, one directory per line, searched in order:
          1) <dist>/../lib and <dist>/../vcpkg_installed/arm64-ohos/lib, whichever exist,
             falling back to <dist>/lib
          2) <project>/entry/build/default/intermediates/libs/default/<abi>/
        Lines end with a trailing slash.
        """
        lines = []

        # 1) dist/lib is stripped by package.py; symbolization needs the UNSTRIPPED libs (same
        # build-id), which live in siblings of dist/: <build>/lib holds WebKit's own libs,
        # vcpkg_installed holds the dependencies.
        dist = Path(self._dist_path)
        unstripped = [d for d in (dist.parent / "lib", dist.parent / "vcpkg_installed" / "arm64-ohos" / "lib")
                      if d.is_dir()]
        for chosen in unstripped or [dist / "lib"]:
            line = chosen.as_posix()
            if not line.endswith("/"):
                line += "/"
            lines.append(line)

        # 2) Project intermediates libs (ABI-mapped)
        proj_intermediates = (
            self._project_root
            / "entry"
            / "build"
            / "default"
            / "intermediates"
            / "libs"
            / "default"
            / self._abi
        )
        project_line = proj_intermediates.as_posix()
        if not project_line.endswith("/"):
            project_line += "/"
        lines.append(project_line)

        # Write the file
        out_path = self._symbolize_file_path()
        out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"[symbolize] Wrote {out_path} with {len(lines)} line(s).")

    # ---------------- public operations ----------------

    def install(self):
        """
        Populate .webkit/<version>/<abi>/{sdk,runtime} from the dist/ tree of a WebKit build.

        vcpkg's .pc files are relocatable (prefix=${pcfiledir}/../..), so the SDK works wherever
        it lands — no prefix rewriting needed.
        """
        dist = Path(self._dist_path)
        dist_lib = dist / "lib"
        if not dist_lib.is_dir():
            raise FileNotFoundError(f"WebKit dist not found: expected {dist_lib}")

        sdk = self._sdk_dir_versioned
        rt = self._runtime_dir_versioned
        for d in (sdk, rt):
            if d.exists():
                shutil.rmtree(d)
            d.mkdir(parents=True, exist_ok=True)

        # --- runtime: the entire lib/ tree (all .so + gstreamer-1.0/ + gio/modules/ +
        #     wpe-webkit-2.0/), symlinks preserved so the versioned soname chains stay intact.
        print(f"[install] runtime <- {dist_lib}")
        shutil.copytree(dist_lib, rt / "lib", symlinks=True, dirs_exist_ok=True)

        # --- sdk: headers, pkg-config, and the link-lib .so chains (top-level *.so* only).
        (sdk / "lib").mkdir(parents=True, exist_ok=True)
        for top in ("include", "share"):
            src = dist / top
            if src.is_dir():
                print(f"[install] sdk/{top} <- {src}")
                shutil.copytree(src, sdk / top, symlinks=True, dirs_exist_ok=True)
        shutil.copytree(dist_lib / "pkgconfig", sdk / "lib" / "pkgconfig",
                        symlinks=True, dirs_exist_ok=True)
        n = 0
        for so in dist_lib.glob("*.so*"):
            if so.is_dir():
                continue
            dst = sdk / "lib" / so.name
            if so.is_symlink():
                rel = os.readlink(so)
                if dst.is_symlink() or dst.exists():
                    dst.unlink()
                os.symlink(rel, dst)
            else:
                shutil.copy2(so, dst, follow_symlinks=False)
            n += 1
        print(f"[install] sdk/lib <- {n} link-lib file(s)")

        # Dev subdirs under lib/ referenced by .pc Cflags (e.g. glib-2.0/include/glibconfig.h).
        # The plugin/module dirs are runtime-only and stay out of the SDK.
        runtime_only = {"pkgconfig", "gstreamer-1.0", "gio", "wpe-webkit-2.0"}
        for sub in dist_lib.iterdir():
            if sub.is_dir() and sub.name not in runtime_only:
                print(f"[install] sdk/lib/{sub.name} <- {sub}")
                shutil.copytree(sub, sdk / "lib" / sub.name, symlinks=True, dirs_exist_ok=True)

        self._update_current(self._current_link, self._version_dir)
        self._write_symbolize_lib_paths()
        print("Done.")

    def make_current(self, version: str):
        """Flip .webkit/current -> <version> and relink entry/libs/<abi>."""
        self._version = version
        self._version_dir = self._webkit_base / self._version
        self._abi_dir_versioned = self._version_dir / self._abi
        self._sdk_dir_versioned = self._abi_dir_versioned / "sdk"
        self._runtime_dir_versioned = self._abi_dir_versioned / "runtime"
        if not self._version_dir.exists():
            raise FileNotFoundError(f"Version directory not found: {self._version_dir}")
        self._update_current(self._current_link, self._version_dir)
        print(f"Now using WebKit version: {version}")

    def clean(self, version: str):
        """Delete .webkit/<version>. If current points to it, remove current symlink."""
        target = self._webkit_base / version
        if not target.exists():
            print(f"Nothing to clean: {target} does not exist.")
            return
        # If current points to this version, remove the symlink
        if self._current_link.is_symlink():
            try:
                cur_target = os.readlink(self._current_link)
                if cur_target == version or (self._current_link.resolve() == target.resolve()):
                    self._current_link.unlink()
                    print("Removed .webkit/current (it pointed to the cleaned version).")
            except OSError:
                pass
        shutil.rmtree(target)
        print(f"Removed {target}")

# ----------------------------------- CLI -----------------------------------

def main():
    p = argparse.ArgumentParser(
        description="Manage WebKit under .webkit/<version>/<abi>/{sdk,runtime} with a 'current' alias."
    )
    sub = p.add_subparsers(dest="cmd", metavar="command", required=True)

    # Common options
    p.add_argument("-a", "--arch", default=Bootstrap.default_arch,
                   choices=["arm64", "aarch64", "x86_64", "amd64"],
                   help="Input architecture (mapped to ABI internally)")

    # install
    install_p = sub.add_parser("install", help="Install a WebKit build into .webkit/<version> and make it current")
    install_p.add_argument("dist", metavar="DIST",
                           help="dist/ tree of a WebKit build, e.g. <webkit>/WebKitBuild/OHOS/Release/dist")
    install_p.add_argument("-v", "--version", default=Bootstrap.default_version,
                           help="Version name of the install (used for .webkit/<version>)")

    # make-current
    mk_p = sub.add_parser("make-current", help="Point .webkit/current to a version and relink entry/libs")
    mk_p.add_argument("version", help="Existing version directory under .webkit to make current")

    # clean
    cl_p = sub.add_parser("clean", help="Remove .webkit/<version> (and current if it points there)")
    cl_p.add_argument("version", help="Version directory to remove under .webkit")

    args = p.parse_args()

    # Normalize args for Bootstrap ctor
    ctor_args = {
        "arch": args.arch,
        "version": getattr(args, "version", Bootstrap.default_version),
        "dist": getattr(args, "dist", None),
    }
    bs = Bootstrap(ctor_args)

    if args.cmd == "install":
        print(f"Config: cmd=install, arch={args.arch} (ABI={ABI_MAP.get(args.arch, args.arch)}), "
              f"version={ctor_args['version']}, dist={ctor_args['dist']}")
        bs.install()
    elif args.cmd == "make-current":
        print(f"Config: cmd=make-current, arch={args.arch}, version={args.version}")
        bs.make_current(args.version)
    elif args.cmd == "clean":
        print(f"Config: cmd=clean, version={args.version}")
        bs.clean(args.version)
    else:
        p.error(f"Unknown command: {args.cmd}")

if __name__ == "__main__":
    sys.exit(main())

