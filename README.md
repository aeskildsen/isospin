# isospin

DSP instruments written in [Faust](https://faust.grame.fr). One source for each instrument, built for several hosts: SuperCollider UGens first, Pd externals and CLAP plugins later.

This repository holds the DSP sources and the scripts that make host code from them. The SuperCollider builds are here: a plugin to install in SuperCollider, and static UGen sources for engines that link the UGens into the server (see below). The other builds are not here yet.

## Instruments

| File | What it is |
|---|---|
| `woodbar.dsp` | A struck wooden bar: claves, wood stick, xylophone or marimba bar. Modal bar with the wood damping law and a Hertz-timed mallet. |
| `wood.lib` | Shared parts for the wooden instruments: damping law, mallet strike, noise modes. |
| `native/superpos/` | SuperPos, a granular UGen in hand-written C++: overlapping grains from a mono buffer, started at a moving read pointer, with random grain times (`regularity`, `probability`), a window shape (`texture`) and a level control by `1/√n` (`normalize`) as in Mutable Instruments Clouds, and with per-grain detune, scatter and pan in stereo. With `normalize` 0 and the other controls at their defaults, its output is the same as a graph of SuperCollider's `Impulse`, `Phasor` and two-channel `GrainBuf` (see `superpos_core.h`). SuperPosLive uses the same grain engine on a circular record buffer: it records its input (any signal: a bus, a UGen, a device input), with freeze, and its grains never read across the record head. See [`native/superpos/README.md`](native/superpos/README.md). |
| `native/pulsar/` | Pulsar, a pulsar synthesis UGen in hand-written C++: three trains of sine grains (the fundamental and two formants) in stereo, after the pulsar SynthDef of Alik Rustamoff's *Musical sound design in SuperCollider*. An `energy` input (usually an envelope from the SynthDef) sets the levels, the grain durations and the LFO rates, so the timbre follows the note. It processes only the grains that play, so it costs less CPU and much less real-time memory than the GrainSin SynthDef. `reference/pulsar.dsp` is the design in Faust; it is not in the release. See [`native/pulsar/Pulsar.md`](native/pulsar/Pulsar.md). |

## Install in SuperCollider

Each [GitHub release](https://github.com/aeskildsen/isospin/releases) has a plugin for scsynth and supernova, with the class files and help pages: `isospin-sc-<version>-linux-x86_64.zip`, `-macos-universal.zip` (Apple silicon and Intel) or `-windows-x86_64.zip`. Needs SuperCollider 3.14 or later. Put the `isospin` folder of the zip into your extension folder (`Platform.userExtensionDir.openOS;` opens it), then recompile the class library. The `README.md` in the zip has the details, also for macOS, where the plugin is not notarized.

## Try it

Compile with the Faust compiler. Give the repository as an include path, so that `woodbar.dsp` finds `wood.lib`:

```sh
faust -I . woodbar.dsp -o woodbar.cpp
```

You can also paste `woodbar.dsp` and `wood.lib` into the [Faust IDE](https://faustide.grame.fr).

## Static SuperCollider UGens

`scripts/build-sc-static.py` makes C++ sources for UGens that are linked into the server binary, as [SuperSonic](https://github.com/samaaron/supersonic) needs (it cannot load plugins at run time). `supercollider.toml` lists the UGens. For each UGen the script runs Faust with the `supercollider.cpp` architecture and then `scripts/faust-static-ugen.py`, which gives each UGen its own names and a `<Class>_Load` function. The output also has the Faust headers, a CMake file with the source list and build settings (`isospin.cmake`) and a header with `Isospin_Load()` (`isospin_sc_static.h`). For sclang, the output has the class file of each UGen (`classes/<Unit>.sc`, for example `classes/PhysWoodbar.sc`). The script makes it from the Faust JSON description, with the same arguments and defaults as `faust2sc.py`.

The output also has a help page for each UGen (`help/<Unit>.schelp`). [md2misc](https://github.com/aeskildsen/md2misc) makes it from the Markdown file next to the UGen source: `woodbar.md`, `native/superpos/SuperPos.md`. For a Faust UGen, the build adds the arguments from the tooltips and ranges of the controls. The build stops if a page is missing, or if its arguments are not those of the class file.

Development build into a folder (writes only the files that changed):

```sh
python3 -I scripts/build-sc-static.py --out build/sc-static
```

Release build (needs a clean working tree; the same commit and Faust version give the same bytes):

```sh
python3 -I scripts/build-sc-static.py --release 0.1.0 --tarball dist
```

Push a tag `v<version>` to release. The workflow `.github/workflows/release.yml` builds `isospin-sc-static-<version>.tar.gz` with Faust from a pinned nixpkgs, builds the plugin zips from it (see the next section), and attaches all of them and their `.sha256` files to a GitHub release. [flux-engine](https://github.com/aeskildsen/flux-engine) takes its UGens from these releases.

Needs Faust (the releases use 2.85.9) and Python 3.11 or later.

md2misc is a git submodule in `external/md2misc`. Clone with `git clone --recurse-submodules`, or run `git submodule update --init`. md2misc is GPL-3.0; it is a build tool and is not in the release.

With Nix, `flake.nix` gives a shell with the same Faust, Python and C++ compiler as CI (the same pinned nixpkgs), and `just`. Run `nix develop`, or `nix develop -c <command>` for one command. With direnv and nix-direnv, run `direnv allow` once; the shell then loads when you go into the folder.

### Loadable plugin

`scripts/build-sc-plugin.py` builds the same sources as a loadable plugin, with CMake (`sc-plugin/CMakeLists.txt`): `isospin` for scsynth and `isospin_supernova` for supernova (`.so` on Linux, `.scx` on macOS and Windows), and installs a SuperCollider extension folder `isospin/` with the plugins, the class files and the help pages. It takes a static build folder or a release tarball (`--static`) and the include folder of the SuperCollider source (`--sc-include`); without `--static` it makes a development build first. `--zip DIR` also writes `isospin-sc-<version>-<platform>.zip`. On macOS the plugin is one universal binary.

`scripts/check-sc-plugin.py` loads the extension folder in scsynth and supernova, as the only user extension, and renders each UGen in non-real-time mode. Each render must have sound and no bad values.

The workflow `.github/workflows/sc-plugin.yml` builds the plugin on Linux (Ubuntu 22.04, so glibc 2.35 or later), macOS and Windows (MSVC), and runs the load check on Linux and macOS. `check.yml` runs it on each pull request with a development build; `release.yml` runs it with the release tarball.

For your own use, `just sc-plugin` builds and checks the plugin in `build/sc-plugin/isospin`, and `just sc-install` links that folder into your user extension folder. After the next `just sc-plugin`, recompile the class library to use the new build. `just sc-install` stops if another copy of an isospin class is installed. Needs CMake 3.16 or later (in the Nix shell).

### Hand-written C++ UGens

A UGen can also be hand-written C++ (a `[[native]]` table in `supercollider.toml`; the comments there give the fields). The release gets its sources without change. Keep the DSP core apart from the UGen wrapper: the core uses no SuperCollider headers, so that test programs can run it under the sanitizers. The wrapper includes `isospin/sc_plugin_table.h`, which makes the same source work in SuperSonic (more than one engine in a process) and with stock scsynth headers. `tests/fixtures/native/fixture` is a small example.

### Build checks

`scripts/check-native.py` makes a development build and compiles every UGen source two ways: against stock SuperCollider headers (linked into a loadable scsynth plugin) and against SuperSonic's headers with the defines of its engine. Then it runs the tests of the hand-written UGens with AddressSanitizer and UndefinedBehaviorSanitizer (with `float-cast-overflow`). SuperPos has a fuzz test among them, `native/superpos/test/superpos_fuzz.cpp`; the checks run it with 10⁵ parameter sets and seed 1, and `superpos_fuzz <sets> <seed>` gives a longer run. By default it uses `tests/fixtures/supercollider.toml`, which has a hand-written test UGen next to woodbar; `--manifest supercollider.toml` checks the release list.

```sh
python3 -I scripts/check-native.py \
    --sc-include <SuperCollider source>/include \
    --supersonic <SuperSonic source>
```

`scripts/check-help.py` renders each help page with SCDoc (SuperCollider 3.14 or later) and compiles the code of the page. Without `sclang` it is skipped.

The workflow `.github/workflows/check.yml` runs both checks on each pull request (`check-native.py` for both lists), with the headers of SuperCollider 3.14.1 and SuperSonic v0.97.0. Needs a C++17 compiler with the sanitizers (GCC from the same nixpkgs in CI).

## Licence

AGPL-3.0-or-later. See `LICENSE`. `scripts/faust-static-ugen.py` is GPL-3.0-or-later. The generated UGen sources contain code from the Faust `supercollider.cpp` architecture (GPL-3.0-or-later) and copies of Faust headers (LGPL-2.1-or-later with the Faust architecture exception); the `NOTICE.md` in each release gives the details.
