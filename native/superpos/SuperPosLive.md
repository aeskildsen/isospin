---
summary: Overlapping grains from a circular record buffer of the input
related: Classes/SuperPos, Classes/GrainBuf
categories: UGens>Buffer, UGens>Generators>Granular
---

## Description

SuperPosLive records its input into a circular buffer and plays grains of the recent input, in two channels (left, right). It can freeze the buffer. The grains are those of [SuperPos](sc:Classes/SuperPos), which plays them from a buffer that does not change.

The buffer is a mono Buffer from the language. Its length is the longest delay.

## Class methods

### *ar

The arguments after `position` are those of SuperPos. Values outside the ranges below are clipped (`pan` is folded). NaN is safe: a NaN or infinite control does not make the output NaN, and the cloud starts again when the control is valid again.

| Argument | Range | What it does |
|---|---|---|
| `in` | any signal | The signal to record: a bus (`In.ar`), another UGen, or a device input (`SoundIn`). A control-rate input gives one value for each block. |
| `bufnum` | | A mono buffer: the record buffer. A buffer with more than one channel, or no buffer, gives silence. |
| `freeze` | ≥ 0.5 is on | Stops the recording. The buffer keeps its content. |
| `position` | 0 to 1 | Where grains start: 0 is the newest input, 1 is the oldest. |
| `grainDur` | seconds | The length of a grain. A grain plays for at most 10 s. |
| `overlap` | | The mean number of grains that play at the same time. Grains start at a mean rate of `overlap / grainDur` per second. |
| `transpose` | semitones | The pitch of all grains. |
| `regularity` | 0 to 1 | 0: a fixed interval between grains. 1: random intervals, as `Dust`. The mean rate does not change. |
| `probability` | 0 to 1 | The chance that a grain time starts a grain, as `CoinGate`. |
| `texture` | 0 to 1 | The window of each new grain: a steep trapezoid at 0, a triangle at 0.5, a Hann window at 1. A change does not change a grain that plays. |
| `detuneStep` | semitones | Each grain gets a random detune in ±`detuneStep`. |
| `scatter` | seconds | Each grain starts a random time up to `scatter` further back from the newest input. |
| `pan` | −1 to 1 | The centre of the grain pan positions. The pan is equal-power. |
| `stereoSpread` | 0 to 1 | Each grain gets a random pan in `pan` ± `stereoSpread`, folded into −1 to 1. |
| `normalize` | ≥ 0.5 is on | On: the output is multiplied by `1/√n`, with `n` the number of grains that play, so the level stays the same at all overlaps. Off: the plain sum of the grains. |
| `maxGrains` | 1 to 1024 | The size of the grain pool, read when the UGen starts. When the pool is full, new grains do not start. Grains that play are never cut. |

Returns: an array of two channels (left, right).

## Behaviour to know

- **The read limit.** A grain never reads across the record head. When a grain cannot stay behind the head over its whole length (a long grain, transposed up, in a short buffer), it does not start. At transpose 0 with no detune every grain fits. When `freeze` changes, or the buffer changes, a grain that would reach the head plays on and fades out over its last 5 ms before the head.
- **Freeze makes a comb.** With `freeze` on, a fixed `position` and no `scatter`, all grains read the same frames, so the output repeats at the grain interval. Use `scatter`, `regularity` or a moving `position` to prevent this.
- **Buffer sample rate.** SuperPosLive records at the server rate, so it ignores the sample rate of its buffer.
- **The other behaviour is that of SuperPos:** repeatable renders with `RandSeed`, the level limits, high density and old SynthDefs. See [SuperPos](sc:Classes/SuperPos).

## Examples

```supercollider
// A live cloud from the first input. The buffer length is the longest delay.
~rec = Buffer.alloc(s, s.sampleRate * 4, 1);

(
x = { |freeze = 0| SuperPosLive.ar(SoundIn.ar(0), ~rec, freeze, position: 0.2,
    grainDur: 0.1, overlap: 6, scatter: 0.3, stereoSpread: 0.5) }.play;
)

// Freeze the buffer, then move through it.
x.set(\freeze, 1);
```
