#!/usr/bin/env python3
# Copyright (C) 2026 Anders Eskildsen
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Load and render check for the loadable SuperCollider plugin.

Takes the extension folder that build-sc-plugin.py installs (isospin/) and
puts it in a new user data folder, as the only user extension. On macOS,
SuperCollider finds the user folders from the system, not from HOME; there
the check gives the paths to sclang (a language configuration file) and to
the servers (-U, the plugin folder of the server and that of the extension).
Then:

  1. sclang compiles the class library with the class files of the extension
     and writes a score for each UGen: the SynthDef, a buffer if the UGen
     needs one, and a synth that plays for a short time.
  2. scsynth renders each score in non-real-time mode, and then supernova
     does the same. Each server loads the plugin from the extension folder.
  3. Each render must be complete, with no error from the server and no
     bad value (CheckBadValues), and each output channel must have sound.

Usage:
    check-sc-plugin.py [--extension DIR] [--sclang PATH] [--scsynth PATH]
                       [--supernova PATH]

    --extension  The extension folder (default: build/sc-plugin/isospin).
    --sclang     The sclang program (default: sclang on PATH).
    --scsynth    The scsynth program (default: scsynth on PATH).
    --supernova  The supernova program (default: supernova on PATH). Without
                 it, the check renders with scsynth only and says so.
"""
import argparse
import array
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Seconds for each run of sclang or of a server.
TIMEOUT = 120

SAMPLE_RATE = 48000

# The smallest peak (of a full scale of 1) that counts as sound.
MIN_PEAK = 1e-3

# Server output that stops the check.
ERRORS = ["not installed", "not found", "FAILURE", "ERROR", "exception", "CheckBadValues"]

# The sclang program. It writes one score for each UGen into %DIR%. RUNNER
# runs it.
SCRIPT = r"""
var dir = "%DIR%";
var check = { |sig| CheckBadValues.ar(sig, 0, 2); Out.ar(0, sig) };
var scores = [
    \woodbar -> [
        SynthDef(\woodbar, { check.(IsoWoodbar.ar(freq: 660, gate: LFPulse.kr(4, 0, 0.5))) })
    ],
    \superpos -> [
        SynthDef(\superpos, { check.(IsoSuperPos.ar(0, 1, 0.05, 4, normalize: 1)) }),
        [\b_alloc, 0, 4800, 1],
        [\b_gen, 0, \sine1, 5, 1, 0.5, 0.3]
    ],
    \pulsar -> [
        SynthDef(\pulsar, { check.(IsoPulsar.ar(110, 0.5)) })
    ],
    \superposlive -> [
        SynthDef(\superposlive, { check.(IsoSuperPosLive.ar(SinOsc.ar(220, 0, 0.5), 0, 0, 0.5, 0.05, 4)) }),
        [\b_alloc, 0, 48000, 1]
    ]
];
{
    scores.do { |pair|
        var name = pair.key, def = pair.value[0], setup = pair.value[1..];
        var messages = [[0.0, [\d_recv, def.asBytes]]]
            ++ setup.collect { |msg| [0.0, msg] }
            ++ [[0.1, [\s_new, name, 1000, 0, 0]], [2.0, [\n_free, 1000]], [2.1, [\c_set, 0, 0]]];
        Score(messages).writeOSCFile(dir +/+ name ++ ".osc");
        ("check-sc-plugin: score " ++ name).postln;
    };
}.value;
"""

# The program that sclang starts. An error in sclang stops the rest of a
# script file, and sclang then does not exit. SCRIPT refers to the classes of
# the extension; if they are missing, SCRIPT does not compile. Thus RUNNER,
# which refers only to the classes of SuperCollider, runs SCRIPT, catches each
# error and always comes to 0.exit.
RUNNER = r"""
{
    if(thisProcess.interpreter.executeFile("%SCRIPT%").isNil) {
        "check-sc-plugin: sclang-error SCRIPT did not compile or run".postln;
    };
}.try { |error|
    ("check-sc-plugin: sclang-error " ++ error.errorString).postln;
};
0.exit;
"""

# The language configuration of sclang on macOS: the default class library and
# the class files of the extension.
LANG_CONFIG = """includePaths:
    - "%CLASSES%"
excludePaths: []
postInlineWarnings: false
excludeDefaultPaths: false
"""

UGENS = ["woodbar", "superpos", "superposlive", "pulsar"]


def find(given: str | None, name: str) -> str | None:
    return given or shutil.which(name)


def peaks(path: Path) -> list[float]:
    """The peak of each channel of a 16-bit WAV file, of a full scale of 1."""
    with wave.open(str(path)) as w:
        channels = w.getnchannels()
        if w.getsampwidth() != 2:
            sys.exit(f"check-sc-plugin: FAIL ({path.name} is not 16-bit)")
        samples = array.array("h", w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        samples.byteswap()
    return [max((abs(s) for s in samples[c::channels]), default=0) / 32768 for c in range(channels)]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--extension", type=Path, default=ROOT / "build" / "sc-plugin" / "isospin")
    parser.add_argument("--sclang")
    parser.add_argument("--scsynth")
    parser.add_argument("--supernova")
    args = parser.parse_args()

    extension = args.extension.resolve()
    if not (extension / "plugins").is_dir():
        sys.exit(f"check-sc-plugin: FAIL ({extension} has no plugins/; run build-sc-plugin.py)")
    sclang = find(args.sclang, "sclang")
    scsynth = find(args.scsynth, "scsynth")
    if not sclang or not scsynth:
        sys.exit("check-sc-plugin: FAIL (needs sclang and scsynth; use --sclang, --scsynth or PATH)")
    servers = [("scsynth", scsynth)]
    supernova = find(args.supernova, "supernova")
    if supernova:
        servers.append(("supernova", supernova))
    else:
        print("check-sc-plugin: no supernova; the check renders with scsynth only", flush=True)

    with tempfile.TemporaryDirectory(prefix="isospin-plugin-") as tmp:
        tmp = Path(tmp)
        data = tmp / "data"
        home = tmp / "home"
        env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(data),
                   XDG_CONFIG_HOME=str(data), LOCALAPPDATA=str(data))
        # sclang starts Qt. On Linux, the offscreen platform needs no display.
        # The macOS build of SuperCollider has only the cocoa platform.
        if sys.platform.startswith("linux"):
            env["QT_QPA_PLATFORM"] = "offscreen"
        home.mkdir()
        explicit = sys.platform == "darwin"
        if explicit:
            ext = tmp / "extension" / "isospin"
        else:
            ext = data / "SuperCollider" / "Extensions" / "isospin"
        shutil.copytree(extension, ext)

        sclang_cmd = [sclang]
        if explicit:
            config = tmp / "sclang_conf.yaml"
            config.write_text(LANG_CONFIG.replace("%CLASSES%", str(ext / "Classes")))
            sclang_cmd += ["-l", str(config)]

        script = tmp / "scores.scd"
        script.write_text(SCRIPT.replace("%DIR%", str(tmp)))
        runner = tmp / "runner.scd"
        runner.write_text(RUNNER.replace("%SCRIPT%", str(script)))
        try:
            done = subprocess.run(sclang_cmd + [str(runner)], env=env, stdin=subprocess.DEVNULL,
                                  capture_output=True, text=True, timeout=TIMEOUT)
        except subprocess.TimeoutExpired as error:
            text = "".join(s.decode(errors="replace") if isinstance(s, bytes) else (s or "")
                           for s in (error.stdout, error.stderr))
            sys.exit(f"{text[-3000:]}\ncheck-sc-plugin: FAIL (sclang did not stop in {TIMEOUT} s)")
        text = done.stdout + done.stderr
        missing = [u for u in UGENS if not (tmp / f"{u}.osc").is_file()]
        if "check-sc-plugin: sclang-error" in text or missing:
            sys.exit(f"{text[-3000:]}\ncheck-sc-plugin: FAIL (sclang wrote no score for "
                     f"{', '.join(missing) or 'all UGens'})")

        failures = []
        for server, program in servers:
            for ugen in UGENS:
                out = tmp / f"{server}-{ugen}.wav"
                cmd = [program, "-N", str(tmp / f"{ugen}.osc"), "_", str(out),
                       str(SAMPLE_RATE), "WAV", "int16", "-o", "2"]
                if explicit:
                    # The macOS application has the plugins of the server
                    # next to it, in plugins/.
                    stock = Path(program).resolve().parent / "plugins"
                    if not stock.is_dir():
                        sys.exit(f"check-sc-plugin: FAIL (no plugin folder {stock})")
                    cmd += ["-U", f"{stock}{os.pathsep}{ext / 'plugins'}"]
                try:
                    run = subprocess.run(cmd, env=env, stdin=subprocess.DEVNULL, capture_output=True,
                                         text=True, timeout=TIMEOUT)
                except subprocess.TimeoutExpired:
                    failures.append(f"{server} {ugen}: no end in {TIMEOUT} s")
                    continue
                log = run.stdout + run.stderr
                errors = [line for line in log.splitlines() if any(e in line for e in ERRORS)]
                if run.returncode != 0 or errors or not out.is_file():
                    failures.append(f"{server} {ugen}: exit code {run.returncode}\n" + "\n".join(
                        errors or log.splitlines()[-20:]))
                    continue
                level = peaks(out)
                quiet = [c for c, p in enumerate(level) if p < MIN_PEAK]
                if quiet:
                    failures.append(f"{server} {ugen}: no sound in channel {quiet} (peaks {level})")
                    continue
                print(f"OK: {server} {ugen}: peaks {', '.join(f'{p:.3f}' for p in level)}", flush=True)

    if failures:
        sys.exit("\n".join(failures) + "\ncheck-sc-plugin: FAIL")
    print("check-sc-plugin: OK")


if __name__ == "__main__":
    main()
