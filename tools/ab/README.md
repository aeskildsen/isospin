# A/B renders

A tool to compare two versions of a sound: render both in NRT with the same events, then compare level, brightness, stereo and, for deterministic sounds, the null residual. It is for the work on this repository, not part of the release:

- a SynthDef made into a UGen (`\cloud` into IsoSuperPos, the pulsar SynthDef into IsoPulsar);
- a UGen before and after a change (an optimisation, a refactor), with the plugin folder of an earlier build on one side;
- a UGen against the graph of core UGens that it must equal.

It is not a test and does not run in CI. With random sounds, "close enough" is a decision for a person, and the renders are for listening.

## Run

```sh
just ab tools/ab/configs/superpos-grainbuf.scd
```

The recipe builds the UGens of this tree as one loadable scsynth plugin (`scripts/build-sc-plugin.py` without supernova, against the SuperCollider headers of CI), renders side `a` and side `b` of the config, and compares them. The renders go to `build/ab/<config name>/a.wav` and `b.wav`.

Needs `sclang` and `scsynth` on PATH (the dev shell does not have them) and the dev shell for the rest (numpy for `compare.py`). sclang compiles only its own class library and the class files of this build, not the installed extensions, so an installed copy of an isospin class does no harm. If the class library is not next to sclang (`../share/SuperCollider/SCClassLibrary`), set `AB_CLASSLIB`. If the core UGens are not at `<resourceDir>/../../lib/SuperCollider/plugins`, set `AB_CORE_PLUGINS`.

## Config

A config is an `.scd` file that returns an Event:

| Key | Default | What it is |
|---|---|---|
| `a`, `b` | | The two sides: `(name: "text", def: SynthDef(...), plugins: "folder")`. `plugins` is optional: a plugin folder for this side instead of the build of this tree, for example an earlier build. |
| `events` | | Optional: a function that gets the SynthDef name and returns a pattern, for example a `Pbind`. Both sides get the same events: the random seed is the same. The SynthDef arguments that the pattern sets reach the synth. |
| `args` | `[]` | Without `events`: one synth at time 0, with these arguments. |
| `setup` | `[]` | OSC messages at time 0, before the SynthDef, for example `[\b_alloc, 0, 4800]`. |
| `duration` | 10 | Seconds of events. |
| `tail` | 2 | Seconds after the events. |
| `seed` | 1234 | The random seed of the events (`thisThread.randSeed`). |
| `channels`, `sampleRate`, `blockSize`, `memSize` | 2, 48000, 64, 65536 | Server options. |

Put the random values of a note that both sides must share (a release time, an overlap) into SynthDef arguments that the pattern sets, not into `Rand` in the SynthDef.

## Output

```
a: ch0 peak 0.0715 rms 0.0101 ch1 peak 0.0854 rms 0.0102
  brightness (Hz) 419; over time: 153 434 274 812 292 475 292 131
  L/R correlation 0.815
b: ...
null: residual -36.3 dB relative to a, with b 0 samples later than a
```

- **brightness**: the power-weighted spectral centroid of the mono sum, for the whole file and for 8 equal parts of it. The power weighting makes it insensitive to a low noise floor.
- **null**: the RMS of `a - b` relative to `a`, at the lag from −4 to 4 samples that gives the smallest residual. It is meaningful only when both sides are deterministic. `-inf` with "bit for bit" means the files are the same.

## Configs

| Config | What it compares |
|---|---|
| `superpos-grainbuf.scd` | IsoSuperPos against the graph of `Impulse`, `Phasor` and `GrainBuf` that `superpos_core.h` names. Must be bit for bit. |
| `pulsar.scd` | IsoPulsar against the GrainSin SynthDef that it comes from, with its pattern. Random: compare the numbers and listen. |
| `pulsar-plain.scd` | IsoPulsar against three GrainSin trains, without random parts. About −36 dB at 220 Hz (grain lengths can differ by one sample). |
