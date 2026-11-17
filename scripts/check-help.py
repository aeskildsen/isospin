#!/usr/bin/env python3
# Copyright (C) 2026 Anders Eskildsen
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Render check for the help pages of the static SuperCollider UGens.

Makes a development build with build-sc-static.py and then, in one headless
run of sclang:

  1. Renders each help page (help/<unit>.schelp) to HTML with SCDoc. An error
     of SCDoc stops the check.
  2. Compiles the code of the examples of each page. The check compiles each
     region alone, as a user runs it in the IDE: a "(" line to the next ")"
     line, or one other line that is not a comment. The class files of the
     build are in the class library, so the examples can use the UGens.

The help pages need SCDoc 3.14 or later (md2misc writes MATH:: and
SUBSUBSECTION::). Without sclang the check is skipped, so that a build
without SuperCollider is possible; --require-sclang makes this an error (CI
uses it).

Usage:
    check-help.py [--manifest FILE] [--sclang PATH] [--require-sclang]

    --manifest        The UGen list (default: supercollider.toml).
    --sclang          The sclang program (default: SCLANG from the
                      environment, else sclang on PATH).
    --require-sclang  Stop with an error if there is no sclang.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The lowest SuperCollider version that renders the pages.
MIN_VERSION = (3, 14)

# Seconds for the sclang run. A run with one page takes a few seconds.
TIMEOUT = 300

# The sclang program. It renders each page in PAGES and compiles the regions of
# its examples. Each result is a line that starts with "check-help:". An error
# in sclang stops the rest of a script file, and sclang then does not exit.
# Thus the program catches each error and always comes to 0.exit.
SCRIPT = r"""
var pages = [%PAGES%];
{
SCDoc.indexAllDocuments(true);
pages.do { |key|
    var doc, codes = List.new, walk;
    SCDoc.prepareHelpForURL(URI("file://" ++ SCDoc.helpTargetDir ++ "/" ++ key ++ ".html"));
    // After a syntax error, SCDoc gives no document; its error is in the output.
    doc = SCDoc.parseFileFull(SCDoc.documents[key].fullPath);
    walk = { |node| if(node.id == \CODEBLOCK) { codes.add(node.text) }; node.children.do(walk) };
    if(doc.notNil) { walk.(doc) };
    codes.do { |code|
        var region = nil, check = { |text|
            if(text.compile.isNil) {
                ("check-help: example-fail " ++ key ++ "\n" ++ text ++ "\ncheck-help: end").postln
            } {
                ("check-help: example-ok " ++ key).postln
            }
        };
        code.split($\n).do { |line|
            var trimmed = line.stripWhiteSpace;
            case
            { region.notNil } {
                region = region ++ "\n" ++ line;
                if(line == ")") { check.(region); region = nil }
            }
            { line == "(" } { region = line }
            { trimmed.isEmpty or: { trimmed.beginsWith("//") } } { }
            { check.(line) };
        };
        if(region.notNil) { check.(region) };
    };
};
"check-help: done".postln;
}.try { |error|
    ("check-help: sclang-error " ++ error.errorString).postln;
};
0.exit;
"""


def find_sclang(given: str | None) -> str | None:
    return given or os.environ.get("SCLANG") or shutil.which("sclang")


def sclang_version(sclang: str) -> tuple[int, ...]:
    done = subprocess.run([sclang, "-v"], capture_output=True, text=True, timeout=60)
    found = re.search(r"sclang (\d+)\.(\d+)", done.stdout + done.stderr)
    if not found:
        sys.exit(f"check-help: `{sclang} -v` gave no version")
    return int(found[1]), int(found[2])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", type=Path, default=ROOT / "supercollider.toml")
    parser.add_argument("--sclang")
    parser.add_argument("--require-sclang", action="store_true")
    args = parser.parse_args()

    sclang = find_sclang(args.sclang)
    if not sclang:
        if args.require_sclang:
            sys.exit("check-help: FAIL (no sclang; use --sclang, SCLANG or PATH)")
        print("check-help: SKIP (no sclang; the help pages were not rendered)")
        return
    version = sclang_version(sclang)
    if version < MIN_VERSION:
        sys.exit(f"check-help: FAIL (sclang {version[0]}.{version[1]}; "
                 f"the help pages need {MIN_VERSION[0]}.{MIN_VERSION[1]} or later)")

    with tempfile.TemporaryDirectory(prefix="isospin-help-") as tmp:
        tmp = Path(tmp)
        out = tmp / "out"
        build = subprocess.run([sys.executable, "-I", str(ROOT / "scripts" / "build-sc-static.py"),
                                "--out", str(out), "--manifest", str(args.manifest.resolve())])
        if build.returncode != 0:
            sys.exit(f"check-help: FAIL (the build stopped, exit code {build.returncode})")

        # The build output as an extension in a new user data folder.
        data = tmp / "data"
        extension = data / "SuperCollider" / "Extensions" / "isospin"
        shutil.copytree(out / "classes", extension / "classes")
        pages = sorted((out / "help").glob("*.schelp")) if (out / "help").is_dir() else []
        if not pages:
            sys.exit("check-help: FAIL (the build made no help pages)")
        source = extension / "HelpSource" / "Classes"
        source.mkdir(parents=True)
        for page in pages:
            shutil.copyfile(page, source / page.name)
        keys = [f"Classes/{page.stem}" for page in pages]

        script = tmp / "check-help.scd"
        script.write_text(SCRIPT.replace("%PAGES%", ", ".join(f'"{k}"' for k in keys)))
        (tmp / "home").mkdir()
        env = dict(os.environ, HOME=str(tmp / "home"), XDG_DATA_HOME=str(data),
                   XDG_CONFIG_HOME=str(data), QT_QPA_PLATFORM="offscreen")
        print(f"== SCDoc {version[0]}.{version[1]}: {', '.join(keys)}", flush=True)
        try:
            done = subprocess.run([sclang, str(script)], env=env, capture_output=True, text=True,
                                  timeout=TIMEOUT)
        except subprocess.TimeoutExpired as error:
            text = (error.stdout or b"").decode(errors="replace")
            sys.exit(f"{text[-3000:]}\ncheck-help: FAIL (sclang did not stop in {TIMEOUT} s)")
        output = done.stdout + done.stderr

        failures = []
        # SCDoc names the file in a line "ERROR: In <path>:", and the lines
        # after it give the place.
        lines = output.splitlines()
        for i, line in enumerate(lines):
            if re.match(r"ERROR: In .*/HelpSource/.+\.schelp:", line):
                failure = "\n".join(lines[i:i + 2])
                # SCDoc reports the error when it indexes and when it renders
                if failure not in failures:
                    failures.append(failure)
        failures += [line.removeprefix("check-help: ") for line in lines
                     if line.startswith("check-help: sclang-error")]
        failures += [f"example does not compile in {m[1]}:\n{m[2]}"
                     for m in re.finditer(r"check-help: example-fail (\S+)\n(.*?)\ncheck-help: end",
                                          output, re.S)]
        for key in keys:
            if not (data / "SuperCollider" / "Help" / f"{key}.html").is_file():
                failures.append(f"SCDoc made no page for {key}")
        if "check-help: done" not in output:
            failures.append(f"sclang stopped before the end (exit code {done.returncode}):\n"
                            + output[-3000:])

        examples = output.count("check-help: example-ok")
        if failures:
            print("\n\n".join(failures))
            sys.exit(f"check-help: FAIL ({len(failures)} problems)")
        print(f"check-help: OK ({len(keys)} pages, {examples} example regions)")


if __name__ == "__main__":
    main()
