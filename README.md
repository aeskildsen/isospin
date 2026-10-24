# isospin

DSP instruments written in [Faust](https://faust.grame.fr). One source for each instrument, built for several hosts: SuperCollider UGens first, Pd externals and CLAP plugins later.

This repository holds the DSP sources and the scripts that make host code from them. The first host output is the static SuperCollider UGen sources (see below); the other builds are not here yet.

## Instruments

| File | What it is |
|---|---|
| `woodbar.dsp` | A struck wooden bar: claves, wood stick, xylophone or marimba bar. Modal bar with the wood damping law and a Hertz-timed mallet. |
| `wood.lib` | Shared parts for the wooden instruments: damping law, mallet strike, noise modes. |

## Try it

Compile with the Faust compiler. Give the repository as an include path, so that `woodbar.dsp` finds `wood.lib`:

```sh
faust -I . woodbar.dsp -o woodbar.cpp
```

You can also paste `woodbar.dsp` and `wood.lib` into the [Faust IDE](https://faustide.grame.fr).

## Static SuperCollider UGens

`scripts/build-sc-static.py` makes C++ sources for UGens that are linked into the server binary, as [SuperSonic](https://github.com/samaaron/supersonic) needs (it cannot load plugins at run time). `supercollider.toml` lists the UGens. For each UGen the script runs Faust with the `supercollider.cpp` architecture and then `scripts/faust-static-ugen.py`, which gives each UGen its own names and a `<Class>_Load` function. The output also has the Faust headers, a CMake file with the source list and build settings (`isospin.cmake`) and a header with `Isospin_Load()` (`isospin_sc_static.h`). For sclang, the output has the class file of each UGen (`classes/<Unit>.sc`, for example `classes/PhysWoodbar.sc`). The script makes it from the Faust JSON description, with the same arguments and defaults as `faust2sc.py`. There are no help files yet.

Development build into a folder (writes only the files that changed):

```sh
python3 -I scripts/build-sc-static.py --out build/sc-static
```

Release build (needs a clean working tree; the same commit and Faust version give the same bytes):

```sh
python3 -I scripts/build-sc-static.py --release 0.1.0 --tarball dist
```

Push a tag `v<version>` to release. The workflow `.github/workflows/release.yml` builds `isospin-sc-static-<version>.tar.gz` with Faust from a pinned nixpkgs and attaches it and its `.sha256` file to a GitHub release. [flux-engine](https://github.com/aeskildsen/flux-engine) takes its UGens from these releases.

Needs Faust (the releases use 2.85.9) and Python 3.11 or later.

## Licence

AGPL-3.0-or-later. See `LICENSE`. `scripts/faust-static-ugen.py` is GPL-3.0-or-later. The generated UGen sources contain code from the Faust `supercollider.cpp` architecture (GPL-3.0-or-later) and copies of Faust headers (LGPL-2.1-or-later with the Faust architecture exception); the `NOTICE.md` in each release gives the details.
