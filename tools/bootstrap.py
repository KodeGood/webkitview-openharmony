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
import re
import shutil
import stat
import subprocess
import sys
import tarfile
from pathlib import Path
from urllib.request import urlretrieve

# -------------------------------- Helpers -------------------------------------

def _safe_extract_xz(tar_path: Path, dest_dir: Path):
    """
    Extracts an .tar.xz into dest_dir with a traversal guard.
    Uses filter='data' for Python 3.14+ default behavior.
    """
    dest_dir.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tar_path, mode="r:xz") as tf:
        base = dest_dir.resolve()
        for m in tf.getmembers():
            target = (dest_dir / m.name).resolve()
            if os.path.commonpath([base]) != os.path.commonpath([base, target.parent]):
                raise RuntimeError(f"Unsafe path in tar: {m.name}")
        tf.extractall(dest_dir, filter="data")

# ------------------------------ ABI handling ----------------------------------

ABI_MAP = {
    "arm64": "arm64-v8a",
    "aarch64": "arm64-v8a",
    "x86_64": "x86_64",
    "amd64": "x86_64",
}

# Libraries that must exist in SDK/lib for linking.
SDK_LINKING_LIBS_DEFAULT = {
    "libglib-2.0.so",
    "libgio-2.0.so",
    "libgobject-2.0.so",
    "libgmodule-2.0.so",
    "libintl.so",
    "libsoup-3.0.so",
    "libwpe-1.0.so",
    "libWPEWebKit-2.0.so"
}

# ------------------------------- Bootstrap ------------------------------------

class Bootstrap:
    default_arch = "arm64"      # input name (mapped via ABI_MAP)
    default_version = "2.50.0"

    # Source of packages (adjust if your hosting path differs)
    _packages_url_template = "" # TODO
    _devel_package_name_template = "wpewebkit-ohos-{arch}-{version}.tar.xz"
    _runtime_package_name_template = "wpewebkit-ohos-{arch}-{version}-runtime.tar.xz"

    def __init__(self, args=None):
        args = vars(args) if args and not isinstance(args, dict) else (args or {})

        self._in_arch = args.get("arch", self.default_arch)
        self._abi = ABI_MAP.get(self._in_arch, self._in_arch)
        self._version = args.get("version", self.default_version)
        self._external_cerbero_build_path = args.get("cerbero")

        if self._external_cerbero_build_path:
            self._external_cerbero_build_path = os.path.realpath(self._external_cerbero_build_path)

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

        # Cache for downloaded tarballs
        self._cache_dir = self._webkit_base / ".cache"
        self._cache_dir.mkdir(parents=True, exist_ok=True)

        # entry/libs/<abi> link
        self._entry_dir = self._project_root / "entry"
        self._entry_libs_abi = self._entry_dir / "libs" / self._abi

        # Optional cerbero tool discovery (only to read package version if provided)
        if self._external_cerbero_build_path:
            cerbero_arch_suffix = self._in_arch.replace("_", "-")
            self._cerbero_root = self._external_cerbero_build_path
            self._cerbero_cmd = [
                os.path.join(self._cerbero_root, "cerbero-uninstalled"),
                "-c",
                os.path.join(self._cerbero_root, "config", f"cross-ohos-{cerbero_arch_suffix}")
            ]
        else:
            self._cerbero_root = None
            self._cerbero_cmd = None

        # Will be set by CLI if provided
        self._cli_sdk_libs = None

    # ------------------- network / packaging -------------------

    def _get_package_version(self, package_name: str) -> str:
        """
        If a Cerbero tree was supplied, ask it for the package version.
        Otherwise use the requested version CLI arg.
        """
        if not self._cerbero_cmd:
            return self._version
        out = subprocess.check_output(self._cerbero_cmd + ["packageinfo", package_name], encoding="utf-8")
        m = re.search(r"Version:\s+([0-9.]+)", out)
        if m:
            return m.group(1)
        raise RuntimeError("Cannot find package version from Cerbero")

    def _download_package(self, version: str, filename: str) -> Path:
        url = self._packages_url_template.format(version=version, filename=filename)
        target = self._cache_dir / filename

        def report(count, block_size, total_size):
            if total_size > 0:
                pct = min(100, int(100 * count * block_size / total_size))
                size = total_size / (1024 * 1024)
                print(f"\r  {url} [{size:.2f} MiB] {pct}% ", end="", flush=True)

        print(f"  {url}...", end="", flush=True)
        urlretrieve(url, target, reporthook=report if sys.stdout.isatty() else None)
        print("\r  download complete".ljust(80))
        return target

    def fetch_packages(self, version: str) -> tuple[Path, Path]:
        dev_name = self._devel_package_name_template.format(arch=self._in_arch, version=version)
        run_name = self._runtime_package_name_template.format(arch=self._in_arch, version=version)

        if self._external_cerbero_build_path:
            print(f"Copying tarballs from {self._external_cerbero_build_path}...")
            dev_src = Path(self._external_cerbero_build_path) / dev_name
            run_src = Path(self._external_cerbero_build_path) / run_name
            if not dev_src.exists() or not run_src.exists():
                raise FileNotFoundError("Cerbero build missing expected tar(s).")
            dev_t = self._cache_dir / dev_name
            run_t = self._cache_dir / run_name
            shutil.copyfile(dev_src, dev_t)
            shutil.copyfile(run_src, run_t)
        else:
            print("Downloading packages...")
            dev_t = self._download_package(version, dev_name)
            run_t = self._download_package(version, run_name)

        return dev_t, run_t

    # ----------------------- extraction ------------------------

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

    def extract(self, dev_tar: Path, run_tar: Path):
        print(f"Extracting SDK -> {self._sdk_dir_versioned}")
        if self._sdk_dir_versioned.exists():
            shutil.rmtree(self._sdk_dir_versioned)
        _safe_extract_xz(dev_tar, self._sdk_dir_versioned)

        print(f"Extracting runtime -> {self._runtime_dir_versioned}")
        if self._runtime_dir_versioned.exists():
            shutil.rmtree(self._runtime_dir_versioned)
        _safe_extract_xz(run_tar, self._runtime_dir_versioned)

        # point .webkit/current -> <version>
        self._update_current(self._current_link, self._version_dir)

    # ---------------- symlink helpers ----------------

    def _libs_to_stage_in_sdk(self) -> set[str]:
        """
        Resolve the set of .so names to copy (not symlink) into SDK/lib.
        Priority: CLI --sdk-lib (can be repeated) > env WPE_OHOS_SDK_LIBS > defaults.
        """
        if self._cli_sdk_libs:
            return set(self._cli_sdk_libs)

        env_val = os.environ.get("WPE_OHOS_SDK_LIBS")
        if env_val:
            return set(x.strip() for x in env_val.split(",") if x.strip())

        return set(SDK_LINKING_LIBS_DEFAULT)

    def stage_direct_link_libs(self):
        """
        Move wanted libraries from runtime/lib to sdk/lib.
        If a file already exists in sdk/lib, we leave it as-is and (optionally) remove
        a stale copy in runtime/lib to guarantee single ownership in SDK.
        """
        wanted = self._libs_to_stage_in_sdk()
        if not wanted:
            return

        sdk_lib = Path(self._sdk_dir_versioned) / "lib"
        rt_lib  = Path(self._runtime_dir_versioned) / "lib"
        sdk_lib.mkdir(parents=True, exist_ok=True)

        missing = []
        moved = 0

        for name in sorted(wanted):
            src = rt_lib / name
            dst = sdk_lib / name

            if dst.exists() and not dst.is_symlink():
                # Already staged; optionally clean up runtime duplicate
                if src.exists():
                    try:
                        src.unlink()
                    except Exception:
                        pass
                continue

            if not src.exists():
                # Nothing to move; warn if also missing in SDK
                if not dst.exists():
                    missing.append(name)
                continue

            # Ensure target slot is clear
            if dst.is_symlink() or dst.exists():
                try:
                    if dst.is_dir():
                        shutil.rmtree(dst)
                    else:
                        dst.unlink()
                except Exception:
                    pass

            shutil.move(str(src), str(dst))
            # ensure readable for build systems
            os.chmod(dst, os.stat(dst).st_mode | stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH)
            moved += 1

        if moved:
            print(f"[sdk] Moved {moved} direct-link .so file(s) into {sdk_lib}")
        if missing:
            print("[sdk][warn] Missing from both runtime/lib and sdk/lib: " + ", ".join(missing))

    def _link_entry_libs_to_runtime(self):
        """
        Make entry/libs/<abi> point to .webkit/current/<abi>/runtime/lib.
        On Windows: try symlink; fallback to directory junction; finally copy.
        """
        target = self._current_link / self._abi / "runtime" / "lib"
        self._entry_libs_abi.parent.mkdir(parents=True, exist_ok=True)

        # Remove existing
        if self._entry_libs_abi.is_symlink():
            self._entry_libs_abi.unlink()
        elif self._entry_libs_abi.exists():
            try:
                shutil.rmtree(self._entry_libs_abi)
            except Exception:
                pass

        # Try symlink
        try:
            rel = os.path.relpath(target, start=self._entry_libs_abi.parent)
            self._entry_libs_abi.symlink_to(rel, target_is_directory=True)
            print(f"Linked (symlink): {self._entry_libs_abi} -> {rel}")
            return
        except (OSError, NotImplementedError):
            pass

        # Windows junction fallback
        if os.name == "nt":
            try:
                subprocess.check_call(["cmd", "/c", "mklink", "/J",
                                       str(self._entry_libs_abi), str(target)])
                print(f"Linked (junction): {self._entry_libs_abi} -> {target}")
                return
            except Exception:
                pass

        # Last resort: copy (not ideal)
        shutil.copytree(target, self._entry_libs_abi)
        print(f"Copied (fallback): {self._entry_libs_abi} <- {target}")

    def _symbolize_file_path(self) -> Path:
        """Return the <project>/.symbolize-lib-paths file path."""
        return self._project_root / ".symbolize-lib-paths"

    def _write_symbolize_lib_paths(self):
        """
        Write <project>/.symbolize-lib-paths with two lines:
          1) <cerbero_root>/build/dist/ohos_<arch>/lib              (if --cerbero provided)
          2) <project>/entry/build/default/intermediates/libs/default/<abi>/
        Lines end with a trailing slash. If cerbero root is not provided, only line 2 is written.
        """
        lines = []

        # 1) Cerbero dist lib (only when cerbero root is provided)
        if self._cerbero_root:
            # Use the input arch token (arm64, aarch64, x86_64, amd64) as provided on CLI
            cerbero_dist_lib = Path(self._cerbero_root) / "build" / "dist" / f"ohos_{self._in_arch}" / "lib"
            cerbero_line = cerbero_dist_lib.as_posix()
            if not cerbero_line.endswith("/"):
                cerbero_line += "/"
            lines.append(cerbero_line)
        else:
            print("[symbolize][warn] --cerbero not provided; writing only the project intermediates path.")

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
        """Fetch/copy, extract, stage direct-link libs, symlink the rest, set current, link entry/libs."""
        version = self._get_package_version("wpewebkit")
        dev_t, run_t = self.fetch_packages(version)
        self.extract(dev_t, run_t)
        self.stage_direct_link_libs()
        self._link_entry_libs_to_runtime()
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
        self._link_entry_libs_to_runtime()
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
    sub = p.add_subparsers(dest="cmd", metavar="command", required=False)

    # Common options
    p.add_argument("-a", "--arch", default=Bootstrap.default_arch,
                   choices=["arm64", "aarch64", "x86_64", "amd64"],
                   help="Input architecture (mapped to ABI internally)")

    # install (default)
    install_p = sub.add_parser("install", help="Fetch/copy, extract, set .webkit/current, stage libs, relink entry/libs")
    install_p.add_argument("-v", "--version", default=Bootstrap.default_version,
                           help="WebKit version to fetch/copy (also used for .webkit/<version>)")
    install_p.add_argument("-c", "--cerbero", metavar="PATH",
                           help="Path to a Cerbero dist containing the tarballs")
    install_p.add_argument("--sdk-lib", action="append", dest="sdk_libs",
                           help="Name of a lib*.so to place as a real file in sdk/lib (can be repeated). "
                                "If omitted, uses env WPE_OHOS_SDK_LIBS or a sensible default set.")

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
        "cerbero": getattr(args, "cerbero", None),
    }
    bs = Bootstrap(ctor_args)
    # pass through CLI-provided --sdk-lib values (if any)
    bs._cli_sdk_libs = getattr(args, "sdk_libs", None)

     # ---- For now, enforce cerbero required for install ----
    if args.cmd in (None, "install") and not ctor_args.get("cerbero"):
        print("Error: --cerbero is required for install. "
              "Fetching from web is currently not supported.", file=sys.stderr)
        sys.exit(1)

    if args.cmd in (None, "install"):
        # When no subcommand is given, behave like 'install'
        print(f"Config: cmd=install, arch={args.arch} (ABI={ABI_MAP.get(args.arch, args.arch)}), "
              f"version={ctor_args['version']}, cerbero={bool(ctor_args['cerbero'])}, "
              f"sdk_libs={bs._cli_sdk_libs or os.environ.get('WPE_OHOS_SDK_LIBS') or 'DEFAULT'}")
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

