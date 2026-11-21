#!/usr/bin/env python3
# Copyright (C) 2026 Anders Eskildsen
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Builds the UGens of isospin as a loadable SuperCollider plugin.

Takes the output of build-sc-static.py (a folder or a release tarball),
builds it with CMake (sc-plugin/CMakeLists.txt) for scsynth and supernova, and
installs a SuperCollider extension folder <out>/isospin:

    plugins/        isospin and isospin_supernova (.so on Linux, else .scx)
    Classes/        the class files
    HelpSource/     the help pages
    README.md, LICENSE, NOTICE.md, BUILDINFO

With --zip, it also writes isospin-sc-<version>-<platform>.zip, with the
isospin folder at the top. The release workflow attaches one zip for each
platform.

Without --static, it first makes a development build of the static sources
into build/sc-static.

Usage:
    build-sc-plugin.py --sc-include DIR [--static DIR_OR_TARBALL]
                       [--out DIR] [--build DIR] [--zip DIR]
                       [--platform NAME] [--no-supernova]

    --sc-include    The SuperCollider header folder that has plugin_interface/,
                    common/ and server/ (the include/ folder of the source).
    --static        A static build folder or a release tarball
                    (isospin-sc-static-<version>.tar.gz).
    --out           The install folder (default: build/sc-plugin).
    --build         The CMake build folder (default: build/sc-plugin-cmake).
    --zip           Write the zip into this folder.
    --platform      The platform name in the zip name (default: linux-<arch>,
                    macos-universal or windows-<arch>).
    --no-supernova  Build the scsynth plugin only.

On macOS, the plugin is one universal binary for arm64 and x86_64.

Needs CMake 3.16 or later, a C++17 compiler and Python 3.11.4 or later. Makes
no network access.
"""
import argparse
import platform
import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The fixed time of each zip entry, so that the same files give the same zip.
ZIP_TIME = (2026, 1, 1, 0, 0, 0)


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def read_buildinfo(static: Path) -> dict[str, str]:
    info = {}
    for line in (static / "BUILDINFO").read_text().splitlines():
        key, _, value = line.partition("=")
        info[key] = value
    return info


def unpack(tarball: Path, dest: Path) -> Path:
    """Unpacks a release tarball into dest and gives its top folder."""
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    with tarfile.open(tarball) as tar:
        tar.extractall(dest, filter="data")
    tops = [p for p in dest.iterdir() if p.is_dir()]
    if len(tops) != 1:
        sys.exit(f"build-sc-plugin: {tarball} must have one top folder")
    return tops[0]


def default_platform() -> str:
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "arm64": "aarch64"}.get(machine, machine)
    if sys.platform == "darwin":
        return "macos-universal"
    if sys.platform == "win32":
        return f"windows-{machine}"
    return f"{sys.platform}-{machine}"


def write_zip(folder: Path, zip_path: Path) -> None:
    """Writes folder into zip_path, with the name of folder at the top. The
    entries are in sorted order, with a fixed time."""
    files = sorted(p for p in folder.rglob("*") if p.is_file())
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in files:
            name = path.relative_to(folder.parent).as_posix()
            entry = zipfile.ZipInfo(name, ZIP_TIME)
            entry.compress_type = zipfile.ZIP_DEFLATED
            mode = 0o755 if path.parent.name == "plugins" else 0o644
            entry.external_attr = (0o100000 | mode) << 16
            z.writestr(entry, path.read_bytes())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sc-include", type=Path, required=True)
    parser.add_argument("--static", type=Path)
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "sc-plugin")
    parser.add_argument("--build", type=Path, default=ROOT / "build" / "sc-plugin-cmake")
    parser.add_argument("--zip", type=Path)
    parser.add_argument("--platform", default=default_platform())
    parser.add_argument("--no-supernova", action="store_true")
    args = parser.parse_args()

    sc_include = args.sc_include.resolve()
    if not (sc_include / "plugin_interface" / "SC_PlugIn.h").is_file():
        sys.exit(f"build-sc-plugin: {sc_include} has no plugin_interface/SC_PlugIn.h")

    build = args.build.resolve()
    if args.static is None:
        static = ROOT / "build" / "sc-static"
        run([sys.executable, "-I", str(ROOT / "scripts" / "build-sc-static.py"), "--out", str(static)])
    elif args.static.is_file():
        static = unpack(args.static.resolve(), build / "static")
    else:
        static = args.static.resolve()
    info = read_buildinfo(static)

    configure = ["cmake", "-S", str(ROOT / "sc-plugin"), "-B", str(build / "cmake"),
                 f"-DISOSPIN_SC_STATIC={static}", f"-DSC_INCLUDE={sc_include}",
                 f"-DISOSPIN_SUPERNOVA={'OFF' if args.no_supernova else 'ON'}"]
    if sys.platform == "darwin":
        configure += ["-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"]
    run(configure)
    run(["cmake", "--build", str(build / "cmake"), "--config", "Release", "--parallel"])

    out = args.out.resolve()
    if (out / "isospin").exists():
        shutil.rmtree(out / "isospin")
    run(["cmake", "--install", str(build / "cmake"), "--config", "Release", "--prefix", str(out)])
    print(f"build-sc-plugin: {info.get('version')}: {out / 'isospin'}")

    if args.zip:
        version = info["version"] if info.get("kind") == "release" else "dev"
        args.zip.mkdir(parents=True, exist_ok=True)
        zip_path = args.zip.resolve() / f"isospin-sc-{version}-{args.platform}.zip"
        write_zip(out / "isospin", zip_path)
        print(f"build-sc-plugin: {zip_path}")


if __name__ == "__main__":
    main()
