# IsoSuperPos and IsoSuperPosLive

Two granular UGens for SuperCollider in hand-written C++, released in isospin from `v0.4.0`.

- **IsoSuperPos** plays overlapping grains from a mono buffer. A read pointer moves through the buffer, and grains start at the pointer.
- **IsoSuperPosLive** records its input into a circular buffer and plays grains of the recent input. It can freeze the buffer.

Both UGens use one grain engine and give two channels (left, right). The design comes from the SynthDef `\cloud` (GrainBuf with random triggers, detune, scatter and pan) and from the granular player of Mutable Instruments Clouds (window shape, level by `1/√n`).

## Use

The help pages have the use, the arguments and the behaviour to know: [IsoSuperPos.md](IsoSuperPos.md) and [IsoSuperPosLive.md](IsoSuperPosLive.md). The release has them as SuperCollider help (`help/IsoSuperPos.schelp`, `help/IsoSuperPosLive.schelp`), which the build makes from the Markdown files with md2misc. The build stops if the arguments of a page are not those of its class file, in the same order.

## Code

| File | Content |
|---|---|
| `superpos_core.h`, `superpos_core.cpp` | The grain engine, with no SuperCollider headers. `Engine<Source>` is a template on the source of the grain start positions: `FileSource` (IsoSuperPos) or `LiveSource` (IsoSuperPosLive). The header gives the source interface and the rules of each control. |
| `superpos_unit.h` | The part that both UGen wrappers share: the grain pool from `RTAlloc`, the seed, the engine inputs, the refusal of SynthDefs with the wrong shape. |
| `SuperPos.cpp`, `SuperPosLive.cpp` | The UGen wrappers. `SuperPos_Load` registers both UGens. |
| `IsoSuperPos.sc`, `IsoSuperPosLive.sc` | The class files. |
| `IsoSuperPos.md`, `IsoSuperPosLive.md` | The help pages. |
| `test/` | The tests (see below). |

Rules for changes:

- **The engine uses static polymorphism**, not virtual functions, so that the per-sample loop stays inline. To add a source, add a class with the interface in `superpos_core.h` and a line `template class Engine<NewSource>;` in `superpos_core.cpp`.
- **Keep the GrainBuf order of operations.** The bit-exact match with GrainBuf (see "The same as GrainBuf" in [IsoSuperPos.md](IsoSuperPos.md)) needs the same float and double types and the same order as `Impulse`, `Phasor`, `BinaryOpUGen` and `GrainBuf` in 3.14.1, and also their constructor behaviour (`Engine::prime()`). A change that makes the code shorter can break the match. The flux-engine test shows it.
- **No mutable global state and no dynamic initialisers.** SuperSonic can run more than one engine in a process, and its web build runs no dynamic initialisers: a `const double` from a function at namespace scope stays 0 there. Use `constexpr`. `scripts/check-native.py` stops on a dynamic initialiser.
- **Licence.** `superpos_core.cpp` adapts code from SuperCollider (GPL) and from Clouds (MIT). Keep its notice when you copy more code from them.

## Tests

`scripts/check-native.py` runs the three tests under AddressSanitizer and UndefinedBehaviorSanitizer (see the main README):

- `superpos_core_test.cpp`: the window, timing, per-grain values, level, pool and the grain-length cap of the file engine.
- `superpos_live_test.cpp`: recording, freeze and the read limit. The input is the write number of each sample, so the output tells which frame a grain read; a step check finds a jump inside a grain.
- `superpos_fuzz.cpp`: random and extreme controls and buffers for both sources (10⁵ sets, seed 1 in CI; `superpos_fuzz <sets> <seed>` for a longer run). It checks for sanitizer reports, NaN or infinite output, and samples above the possible peak. CI runs it with one fixed seed, so it is a regression test; to look for new failures, run `superpos_fuzz <sets> <seed>` with other seeds, which finds more than a larger set count.

[flux-engine](https://github.com/aeskildsen/flux-engine) tests the released UGens in its engine: `test/native-superpos.sh` compares IsoSuperPos with the GrainBuf graph of [IsoSuperPos.md](IsoSuperPos.md) in stock scsynth (the difference must be exactly 0), compares headless SuperSonic with scsynth, and checks `RandSeed`, `maxGrains`, the refusal of an old SynthDef and the live renders. Its web tests play both UGens in the browser build.

In a non-real-time render with stock scsynth 3.14.1, free the synth before the end of the score: scsynth can crash at the end while IsoSuperPos or GrainBuf still runs (the WAV file is complete).

## CPU

Measured on 2026-11-14 in stock scsynth NRT, per synth, as a percentage of one core: IsoSuperPos 0.060, 0.215 and 0.641 at overlap 5, 20 and 60; the grain part of `\cloud` 0.107, 0.257 and 0.649. In the SuperSonic web build, 512 or more, 128 to 256, and 64 voices play in real time at the same overlaps.
