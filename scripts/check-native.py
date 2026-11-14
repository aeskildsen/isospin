#!/usr/bin/env python3
# Copyright (C) 2026 Anders Eskildsen
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build checks for the static SuperCollider UGen sources.

Makes a development build with build-sc-static.py and then:

  1. Compiles every UGen source against stock SuperCollider headers and links
     them, with isospin_sc_static.h, into a loadable scsynth plugin. The link
     does not allow undefined symbols, so a load function that is declared
     but not defined (or that has the wrong linkage) stops the check.
  2. Compiles every UGen source against SuperSonic's headers, with the
     defines of SuperSonic's engine (CLOCKWORK_GUEST and others), and links
     them into a shared library in the same way. Only for this link, a test
     file defines SuperSonic's constant interface table.
     Hand-written sources must need no dynamic initialisation here (see
     check_static_init).
  3. Builds each test program of the hand-written UGens ([[native]] `tests`)
     with its DSP core (`core`), with AddressSanitizer and
     UndefinedBehaviorSanitizer, and runs it. Exit code 0 is a pass.

Hand-written UGen sources compile with -Wall -Wextra -Werror. The
SuperCollider and SuperSonic headers are system headers here, so that their
own warnings do not stop the check. The UGen wrapper sources (`plugin`) do
not get the two uninitialised-variable warnings: the server constructs each
unit with placement new in memory that it filled before, and GCC sees the
fields of SCUnit as uninitialised. The DSP core (`core`) keeps them.

Usage:
    check-native.py --sc-include DIR --supersonic DIR [--manifest FILE]
                    [--work DIR]

    --sc-include  The SuperCollider header folder that has plugin_interface/,
                  common/ and server/: <prefix>/include/SuperCollider of an
                  installation, or include/ of a source tree.
    --supersonic  A SuperSonic source tree (the repository root).
    --manifest    The UGen list (default: tests/fixtures/supercollider.toml,
                  which has a Faust UGen and a hand-written test UGen).
    --work        The work folder (default: a temporary folder).

Needs faust, a C++17 compiler (CXX, default c++) with the sanitizers, and
Python 3.11 or later.
"""
import argparse
import importlib.util
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The defines of SuperSonic's engine (dsp/scsynth/CMakeLists.txt) that shape
# its headers or the plugin macros.
GUEST_DEFINES = ["CLOCKWORK_GUEST=1", "STATIC_PLUGINS=1", "NO_LIBSNDFILE=1", "SC_AUDIO_API=3"]

# The include folders of SuperSonic's engine, relative to dsp/scsynth.
GUEST_INCLUDES = [
    ".",
    "synth/include/common",
    "synth/include/plugin_interface",
    "synth/include/server",
    "synth/common",
    "synth/server",
    "synth/external_libraries",
    "synth/external_libraries/nova-simd",
]

STOCK_INCLUDES = ["plugin_interface", "common", "server"]

NATIVE_WARNINGS = ["-Wall", "-Wextra", "-Werror"]
PLUGIN_WARNINGS = NATIVE_WARNINGS + ["-Wno-uninitialized", "-Wno-maybe-uninitialized"]

# float-cast-overflow (a float to int conversion of a value out of range, NaN
# included) is not in GCC's "undefined" group.
SANITIZERS = ["-fsanitize=address,undefined,float-cast-overflow", "-fno-sanitize-recover=all",
              "-fno-omit-frame-pointer", "-g", "-O1"]

STOCK_LOAD = """\
// A loadable scsynth plugin with all UGens of the build.
#include <SC_PlugIn.h>

#include "isospin_sc_static.h"

PluginLoad(Isospin) { Isospin_Load(inTable); }
"""

GUEST_LOAD = """\
// All UGens of the build, linked as SuperSonic links them. The engine defines
// gInterfaceTable; this file defines it only so that the link is complete.
#include <SC_PlugIn.h>

#include "isospin_sc_static.h"

const InterfaceTable gInterfaceTable {};

void IsospinCheck_Load(InterfaceTable* table) { Isospin_Load(table); }
"""


def load_build_script():
    sys.dont_write_bytecode = True
    path = ROOT / "scripts" / "build-sc-static.py"
    spec = importlib.util.spec_from_file_location("build_sc_static", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(args: list[str], **kwargs) -> None:
    print("+", shlex.join(args), flush=True)
    result = subprocess.run(args, **kwargs)
    if result.returncode != 0:
        sys.exit(f"check-native: FAIL (exit code {result.returncode})")


class Variant:
    """One way to compile the UGen sources: a header set and its defines."""

    def __init__(self, name: str, system_includes: list[Path], defines: list[str], load: str,
                 check_init: bool):
        self.name = name
        self.system_includes = system_includes
        self.defines = defines
        self.load = load
        # Check the hand-written sources for dynamic initialisation. Only for
        # SuperSonic's headers: the stock headers have dynamic constants of
        # their own (for example pi), which SuperSonic makes constexpr.
        self.check_init = check_init


def check_static_init(objects: list[Path]) -> None:
    """Stops when a hand-written source needs dynamic initialisation. GCC
    gives such a file a _GLOBAL__sub_I_ function, which the .init_array
    section calls; SuperSonic's web build does not call it. The objects must
    be compiled with -O0 -fno-builtin: with optimisation, GCC computes some
    values at compile time (std::acos(-1.)) that Emscripten does not."""
    for obj in objects:
        symbols = subprocess.run(["nm", str(obj)], check=True, capture_output=True, text=True).stdout
        if "_GLOBAL__sub_I_" in symbols:
            sys.exit(f"check-native: FAIL: {obj.name} needs dynamic initialisation of a "
                     "namespace-scope or static variable; SuperSonic's web build does not run it "
                     "(use constexpr values)")


def compile_variant(cxx: list[str], variant: Variant, out: Path, work: Path,
                    ugens: list[dict], natives: list[dict]) -> None:
    print(f"== {variant.name}: compile and link", flush=True)
    obj_dir = work / variant.name
    obj_dir.mkdir(parents=True, exist_ok=True)
    base = cxx + ["-std=c++17", "-O2", "-fPIC", "-c"]
    base += [f"-isystem{p}" for p in variant.system_includes]
    base += [f"-D{d}" for d in variant.defines]
    objects = []
    native_objects = []

    for u in ugens:
        obj = obj_dir / f"{u['class']}.o"
        run(base + [f"-I{out / 'include'}", f'-DSC_FAUST_PREFIX="{u["prefix"]}"',
                    str(out / "src" / f"{u['class']}.cpp"), "-o", str(obj)])
        objects.append(obj)

    for n in natives:
        sources = [(rel, NATIVE_WARNINGS) for rel in n["core"]]
        sources += [(rel, PLUGIN_WARNINGS) for rel in n["plugin"]]
        for rel, warnings in sources:
            obj = obj_dir / n["name"] / (rel.replace("/", "_") + ".o")
            obj.parent.mkdir(parents=True, exist_ok=True)
            run(base + warnings
                + [f"-I{out / 'include'}", f"-I{out / 'src' / n['name']}",
                   str(out / "src" / n["name"] / rel), "-o", str(obj)])
            objects.append(obj)
            if not variant.check_init:
                continue
            # A second object without optimisation, for check_static_init.
            plain = obj.with_suffix(".O0.o")
            run([a for a in base if a != "-O2"] + ["-O0", "-fno-builtin"]
                + [f"-I{out / 'include'}", f"-I{out / 'src' / n['name']}",
                   str(out / "src" / n["name"] / rel), "-o", str(plain)])
            native_objects.append(plain)

    load_src = obj_dir / "isospin_load.cpp"
    load_src.write_text(variant.load)
    load_obj = obj_dir / "isospin_load.o"
    run(base + PLUGIN_WARNINGS + [f"-I{out}", str(load_src), "-o", str(load_obj)])
    objects.append(load_obj)

    check_static_init(native_objects)

    library = obj_dir / "isospin.so"
    run(cxx + ["-shared", "-Wl,--no-undefined", "-o", str(library)]
        + [str(o) for o in objects] + ["-lm"])
    print(f"OK: {variant.name}: {library}", flush=True)


def run_tests(cxx: list[str], work: Path, natives: list[dict]) -> int:
    count = 0
    for n in natives:
        folder = ROOT / n["dir"]
        for test in n["tests"]:
            print(f"== {n['name']}: {test} with sanitizers", flush=True)
            exe = work / "tests" / n["name"] / Path(test).stem
            exe.parent.mkdir(parents=True, exist_ok=True)
            sources = [str(folder / rel) for rel in n["core"]] + [str(folder / test)]
            run(cxx + ["-std=c++17"] + SANITIZERS + NATIVE_WARNINGS
                + [f"-I{folder}"] + sources + ["-o", str(exe)])
            env = dict(os.environ)
            env.setdefault("ASAN_OPTIONS", "abort_on_error=0:halt_on_error=1")
            env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1")
            run([str(exe)], env=env)
            count += 1
    return count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sc-include", type=Path, required=True)
    parser.add_argument("--supersonic", type=Path, required=True)
    parser.add_argument("--manifest", type=Path,
                        default=ROOT / "tests" / "fixtures" / "supercollider.toml")
    parser.add_argument("--work", type=Path)
    args = parser.parse_args()

    sc_include = args.sc_include.resolve()
    for sub in STOCK_INCLUDES:
        if not (sc_include / sub).is_dir():
            sys.exit(f"check-native: no {sub}/ in {sc_include}")
    scsynth = args.supersonic.resolve() / "dsp" / "scsynth"
    if not (scsynth / "synth" / "include" / "plugin_interface").is_dir():
        sys.exit(f"check-native: {args.supersonic} is not a SuperSonic source tree")

    cxx = shlex.split(os.environ.get("CXX", "c++"))
    build = load_build_script()
    manifest = args.manifest.resolve()
    ugens, natives = build.read_manifest(manifest)

    with tempfile.TemporaryDirectory() as tmp:
        work = (args.work or Path(tmp)).resolve()
        out = work / "out"
        print(f"== Development build of {manifest}", flush=True)
        run([sys.executable, "-I", str(ROOT / "scripts" / "build-sc-static.py"),
             "--out", str(out), "--manifest", str(manifest)])

        stock = Variant("stock", [sc_include / s for s in STOCK_INCLUDES], [], STOCK_LOAD,
                        check_init=False)
        guest = Variant("supersonic", [scsynth / s for s in GUEST_INCLUDES],
                        GUEST_DEFINES, GUEST_LOAD, check_init=True)
        for variant in (stock, guest):
            compile_variant(cxx, variant, out, work, ugens, natives)

        tests = run_tests(cxx, work, natives)

    print(f"check-native: OK ({len(ugens)} Faust UGens, {len(natives)} hand-written "
          f"UGens, {tests} tests)")


if __name__ == "__main__":
    main()
