---
summary: Overlapping grains from a mono buffer
related: Classes/IsoSuperPosLive, Classes/GrainBuf
categories: UGens>Buffer, UGens>Generators>Granular
---

## Description

IsoSuperPos plays overlapping grains from a mono buffer, in two channels (left, right). A read pointer moves through the buffer, and grains start at the pointer. [IsoSuperPosLive](sc:Classes/IsoSuperPosLive) plays the same grains from a circular record buffer of its input.

The design comes from the SynthDef `\cloud` (GrainBuf with random triggers, detune, scatter and pan) and from the granular player of Mutable Instruments Clouds (window shape, level by `1/√n`).

## Class methods

### *ar

Values outside the ranges below are clipped (`pan` is folded). NaN is safe: a NaN or infinite control does not make the output NaN, and the cloud starts again when the control is valid again.

| Argument | Range | What it does |
|---|---|---|
| `bufnum` | | A mono buffer. A buffer with more than one channel, or no buffer, gives silence. |
| `pointerRate` | any | The speed of the read pointer: 1 is the speed of the buffer, 0 stops the pointer, a negative value goes backwards. The pointer wraps at the buffer end. |
| `grainDur` | seconds | The length of a grain. A grain plays for at most 10 s. |
| `overlap` | | The mean number of grains that play at the same time. Grains start at a mean rate of `overlap / grainDur` per second. |
| `transpose` | semitones | The pitch of all grains. |
| `regularity` | 0 to 1 | 0: a fixed interval between grains. 1: random intervals, as `Dust`. The mean rate does not change. |
| `probability` | 0 to 1 | The chance that a grain time starts a grain, as `CoinGate`. |
| `texture` | 0 to 1 | The window of each new grain: a steep trapezoid at 0, a triangle at 0.5, a Hann window at 1. A change does not change a grain that plays. |
| `detuneStep` | semitones | Each grain gets a random detune in ±`detuneStep`. |
| `scatter` | seconds | Each grain starts a random time up to `scatter` after the pointer. |
| `pan` | −1 to 1 | The centre of the grain pan positions. The pan is equal-power. |
| `stereoSpread` | 0 to 1 | Each grain gets a random pan in `pan` ± `stereoSpread`, folded into −1 to 1. |
| `normalize` | ≥ 0.5 is on | On: the output is multiplied by `1/√n`, with `n` the number of grains that play, so the level stays the same at all overlaps. Off: the plain sum of the grains. |
| `maxGrains` | 1 to 1024 | The size of the grain pool, read when the UGen starts. When the pool is full, new grains do not start. Grains that play are never cut. |

Returns: an array of two channels (left, right).

## Behaviour to know

- **Repeatable renders.** The random values come from the random generator of the synth. Put `RandSeed.ir(1, seed)` before IsoSuperPos in the graph to get the same render each time. scsynth seeds the generator from the clock, so without `RandSeed` two renders differ.
- **Buffer sample rate.** IsoSuperPos reads at the buffer's own rate (as `BufRateScale`). `\cloud` applies `BufRateScale` twice; IsoSuperPos applies it once.
- **Level limits.** The level control holds for grains that do not correlate: a noise buffer, or a pointer that does not move with the grains. With `pointerRate` 1 and no detune, all grains read the same frame at the same time and add in phase. With random timing, very short grains and a low overlap, the level can change by about 1 dB with the grain length and 0.5 dB with the block size.
- **High density.** With `regularity` above 0, a trigger in the sample after a trigger starts no grain. Above a few thousand grains a second, the mean rate is lower than `overlap / grainDur`.
- **Old SynthDefs.** A SynthDef made with an older class file (from before the first release) can have a IsoSuperPos UGen with one output or too few inputs. The UGen then gives silence and prints a message: make the SynthDef again.

### The same as GrainBuf

With `normalize` 0 and the other controls at their defaults, IsoSuperPos gives the same output, bit for bit, as this graph in SuperCollider 3.14.1, while the buffer length does not change:

```supercollider
(
{ |buf, pointerRate = 1, grainDur = 0.025, overlap = 5, transpose = 0, maxGrains = 64|
    var trig = Impulse.ar(overlap / grainDur);
    var pos = Phasor.ar(0, pointerRate * BufRateScale.kr(buf), 0, BufFrames.kr(buf)) / BufFrames.kr(buf);
    GrainBuf.ar(2, trig, grainDur, buf, transpose.midiratio, pos, 2, 0, -1, maxGrains + 1)
}
)
```

Use `overlap / grainDur`, not `overlap * grainDur.reciprocal`: SuperCollider calculates `reciprocal` with an approximation on x86, and some grains then start one sample earlier. GrainBuf needs `maxGrains + 1`, because it starts no grain when its pool has one free slot.

## Examples

```supercollider
// A file cloud. b is a mono Buffer.
b = Buffer.read(s, Platform.resourceDir +/+ "sounds/a11wlk01.wav");

(
{ IsoSuperPos.ar(b, pointerRate: 0.5, grainDur: 0.08, overlap: 8, regularity: 0.7,
    detuneStep: 0.1, scatter: 0.05, stereoSpread: 0.6) }.play;
)

// The same render each time.
(
{
    RandSeed.ir(1, 42);
    IsoSuperPos.ar(b, grainDur: 0.05, overlap: 12, regularity: 1, detuneStep: 0.3, stereoSpread: 1)
}.play;
)
```
