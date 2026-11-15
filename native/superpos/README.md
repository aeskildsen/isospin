# SuperPos and SuperPosLive

Two granular UGens for SuperCollider in hand-written C++, released in isospin from `v0.4.0`.

- **SuperPos** plays overlapping grains from a mono buffer. A read pointer moves through the buffer, and grains start at the pointer.
- **SuperPosLive** records its input into a circular buffer and plays grains of the recent input. It can freeze the buffer.

Both UGens use one grain engine and give two channels (left, right). The design comes from the SynthDef `\cloud` (GrainBuf with random triggers, detune, scatter and pan) and from the granular player of Mutable Instruments Clouds (window shape, level by `1/√n`).

## Use

```supercollider
// A file cloud. b is a mono Buffer.
{ SuperPos.ar(b, pointerRate: 0.5, grainDur: 0.08, overlap: 8, regularity: 0.7,
    detuneStep: 0.1, scatter: 0.05, stereoSpread: 0.6) }.play;

// A live cloud from the first input. The buffer length is the longest delay.
~rec = Buffer.alloc(s, s.sampleRate * 4, 1);
x = { |freeze = 0| SuperPosLive.ar(SoundIn.ar(0), ~rec, freeze, position: 0.2,
    grainDur: 0.1, overlap: 6, scatter: 0.3, stereoSpread: 0.5) }.play;
x.set(\freeze, 1);
```

## Arguments

The class files `SuperPos.sc` and `SuperPosLive.sc` give the defaults.

| Argument | Range | What it does |
|---|---|---|
| `bufnum` | | A mono buffer. A buffer with more than one channel, or no buffer, gives silence. |
| `pointerRate` | any (SuperPos only) | The speed of the read pointer: 1 is the speed of the buffer, 0 stops the pointer, a negative value goes backwards. The pointer wraps at the buffer end. |
| `in` | any signal (SuperPosLive only) | The signal to record: a bus (`In.ar`), another UGen, or a device input (`SoundIn`). A control-rate input gives one value for each block. |
| `freeze` | ≥ 0.5 is on (SuperPosLive only) | Stops the recording. The buffer keeps its content. |
| `position` | 0 to 1 (SuperPosLive only) | Where grains start: 0 is the newest input, 1 is the oldest. |
| `grainDur` | seconds | The length of a grain. A grain plays for at most 10 s. |
| `overlap` | | The mean number of grains that play at the same time. Grains start at a mean rate of `overlap / grainDur` per second. |
| `transpose` | semitones | The pitch of all grains. |
| `regularity` | 0 to 1 | 0: a fixed interval between grains. 1: random intervals, as `Dust`. The mean rate does not change. |
| `probability` | 0 to 1 | The chance that a grain time starts a grain, as `CoinGate`. |
| `texture` | 0 to 1 | The window of each new grain: a steep trapezoid at 0, a triangle at 0.5, a Hann window at 1. A change does not change a grain that plays. |
| `detuneStep` | semitones | Each grain gets a random detune in ±`detuneStep`. |
| `scatter` | seconds | Each grain starts a random time up to `scatter` after the pointer (SuperPos) or further back from the newest input (SuperPosLive). |
| `pan` | −1 to 1 | The centre of the grain pan positions. The pan is equal-power. |
| `stereoSpread` | 0 to 1 | Each grain gets a random pan in `pan` ± `stereoSpread`, folded into −1 to 1. |
| `normalize` | ≥ 0.5 is on | On: the output is multiplied by `1/√n`, with `n` the number of grains that play, so the level stays the same at all overlaps. Off: the plain sum of the grains. |
| `maxGrains` | 1 to 1024 | The size of the grain pool, read when the UGen starts. When the pool is full, new grains do not start. Grains that play are never cut. |

Values outside these ranges are clipped (`pan` is folded). NaN is safe: a NaN or infinite control does not make the output NaN, and the cloud starts again when the control is valid again.

## Behaviour to know

- **Repeatable renders.** The random values come from the random generator of the synth. Put `RandSeed.ir(1, seed)` before SuperPos in the graph to get the same render each time. scsynth seeds the generator from the clock, so without `RandSeed` two renders differ.
- **The same as GrainBuf.** With `normalize` 0 and the other controls at their defaults, SuperPos gives the same output, bit for bit, as this graph in SuperCollider 3.14.1, while the buffer length does not change:

  ```supercollider
  trig = Impulse.ar(overlap / grainDur);
  pos  = Phasor.ar(0, pointerRate * BufRateScale.kr(buf), 0, BufFrames.kr(buf)) / BufFrames.kr(buf);
  GrainBuf.ar(2, trig, grainDur, buf, transpose.midiratio, pos, 2, 0, -1, maxGrains + 1)
  ```

  Use `overlap / grainDur`, not `overlap * grainDur.reciprocal`: SuperCollider calculates `reciprocal` with an approximation on x86, and some grains then start one sample earlier. GrainBuf needs `maxGrains + 1`, because it starts no grain when its pool has one free slot.
- **Buffer sample rate.** SuperPos reads at the buffer's own rate (as `BufRateScale`). SuperPosLive records at the server rate, so it ignores the sample rate of its buffer. `\cloud` applies `BufRateScale` twice; SuperPos applies it once.
- **The read limit in SuperPosLive.** A grain never reads across the record head. When a grain cannot stay behind the head over its whole length (a long grain, transposed up, in a short buffer), it does not start. At transpose 0 with no detune every grain fits. When `freeze` changes, or the buffer changes, a grain that would reach the head plays on and fades out over its last 5 ms before the head.
- **Freeze makes a comb.** With `freeze` on, a fixed `position` and no `scatter`, all grains read the same frames, so the output repeats at the grain interval. Use `scatter`, `regularity` or a moving `position` to prevent this.
- **Level limits.** The level control holds for grains that do not correlate: a noise buffer, or a pointer that does not move with the grains. With `pointerRate` 1 and no detune, all grains read the same frame at the same time and add in phase. With random timing, very short grains and a low overlap, the level can change by about 1 dB with the grain length and 0.5 dB with the block size.
- **High density.** With `regularity` above 0, a trigger in the sample after a trigger starts no grain. Above a few thousand grains a second, the mean rate is lower than `overlap / grainDur`.
- **Old SynthDefs.** A SynthDef made with an older class file (from before the first release) can have a SuperPos UGen with one output or too few inputs. The UGen then gives silence and prints a message: make the SynthDef again.

## Code

| File | Content |
|---|---|
| `superpos_core.h`, `superpos_core.cpp` | The grain engine, with no SuperCollider headers. `Engine<Source>` is a template on the source of the grain start positions: `FileSource` (SuperPos) or `LiveSource` (SuperPosLive). The header gives the source interface and the rules of each control. |
| `superpos_unit.h` | The part that both UGen wrappers share: the grain pool from `RTAlloc`, the seed, the engine inputs, the refusal of SynthDefs with the wrong shape. |
| `SuperPos.cpp`, `SuperPosLive.cpp` | The UGen wrappers. `SuperPos_Load` registers both UGens. |
| `SuperPos.sc`, `SuperPosLive.sc` | The class files. |
| `test/` | The tests (see below). |

Rules for changes:

- **The engine uses static polymorphism**, not virtual functions, so that the per-sample loop stays inline. To add a source, add a class with the interface in `superpos_core.h` and a line `template class Engine<NewSource>;` in `superpos_core.cpp`.
- **Keep the GrainBuf order of operations.** The bit-exact match needs the same float and double types and the same order as `Impulse`, `Phasor`, `BinaryOpUGen` and `GrainBuf` in 3.14.1, and also their constructor behaviour (`Engine::prime()`). A change that makes the code shorter can break the match. The flux-engine test shows it.
- **No mutable global state and no dynamic initialisers.** SuperSonic can run more than one engine in a process, and its web build runs no dynamic initialisers: a `const double` from a function at namespace scope stays 0 there. Use `constexpr`. `scripts/check-native.py` stops on a dynamic initialiser.
- **Licence.** `superpos_core.cpp` adapts code from SuperCollider (GPL) and from Clouds (MIT). Keep its notice when you copy more code from them.

## Tests

`scripts/check-native.py` runs the three tests under AddressSanitizer and UndefinedBehaviorSanitizer (see the main README):

- `superpos_core_test.cpp`: the window, timing, per-grain values, level, pool and the grain-length cap of the file engine.
- `superpos_live_test.cpp`: recording, freeze and the read limit. The input is the write number of each sample, so the output tells which frame a grain read; a step check finds a jump inside a grain.
- `superpos_fuzz.cpp`: random and extreme controls and buffers for both sources (10⁶ sets, seed 1 in CI; `superpos_fuzz <sets> <seed>` for a longer run). It checks for sanitizer reports, NaN or infinite output, and samples above the possible peak.

[flux-engine](https://github.com/aeskildsen/flux-engine) tests the released UGens in its engine: `test/native-superpos.sh` compares SuperPos with the GrainBuf graph above in stock scsynth (the difference must be exactly 0), compares headless SuperSonic with scsynth, and checks `RandSeed`, `maxGrains`, the refusal of an old SynthDef and the live renders. Its web tests play both UGens in the browser build.

In a non-real-time render with stock scsynth 3.14.1, free the synth before the end of the score: scsynth can crash at the end while SuperPos or GrainBuf still runs (the WAV file is complete).

## CPU

Measured on 2026-11-14 in stock scsynth NRT, per synth, as a percentage of one core: SuperPos 0.060, 0.215 and 0.641 at overlap 5, 20 and 60; the grain part of `\cloud` 0.107, 0.257 and 0.649. In the SuperSonic web build, 512 or more, 128 to 256, and 64 voices play in real time at the same overlaps.
