# isospin

DSP instruments written in [Faust](https://faust.grame.fr). One source for each instrument, built for several hosts: SuperCollider UGens first, Pd externals and CLAP plugins later.

This repository holds only the DSP sources. The builds for each host are not here yet.

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

## Licence

AGPL-3.0-or-later. See `LICENSE`.
