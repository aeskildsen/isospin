---
summary: Pulsar synthesis with three trains of sine grains
related: Classes/GrainSin, Classes/Env
categories: UGens>Generators>Granular
---

## Description

Pulsar plays three trains of sine grains, in two channels (left, right). Each train starts a grain at each period of `freq`. A grain is a short sine under a Hann window, as a grain of [GrainSin](sc:Classes/GrainSin).

- Train 0 is the fundamental: its grains have the frequency `freq`.
- Train 1 is the first formant: its grains have the frequency `freq * formant1`.
- Train 2 is the second formant: its grains have the frequency `freq * formant2`.

The grain duration is `overlap` periods of `freq`. A short grain gives a wide, buzzy formant. A long grain gives a narrow formant, near a pure sine.

The `energy` input is the energy of the note over time, from 0 to 1. Usually it is an envelope, but any control signal can be used. Energy controls the sound in three ways:

1. **Level.** The formants have larger powers than the fundamental, so when the energy falls, the formants fade before the fundamental. A high note fades to a pure sound sooner than a low note.
2. **Grain duration.** With `law` 0 the grains are short when the energy is high (bright, buzzy) and become longer as it falls (purer). `law` 1 gives the opposite.
3. **Wobble.** Slow sine waves (LFOs) move the grain durations and frequencies. Their rate follows the energy, so they slow down and stop at the end of the note.

Pink noise on the energy (`jitter`) makes each note a little different, and each channel different from the other.

`velocity` makes the note louder and brighter together, as on an acoustic instrument.

The design comes from the pulsar SynthDef of Alik Rustamoff's video series *Musical sound design in SuperCollider* (episode 1, pulsar synthesis), which uses three GrainSin UGens. Pulsar makes the same sound with less work for the processor and much less real-time memory.

## Class methods

### *ar

Values outside the ranges below are clipped. NaN is safe: a NaN or infinite control gets its default value (`freq` keeps its last valid value), and the output stays finite.

| Argument | Range | What it does |
|---|---|---|
| `freq` | Hz, 1 to half the sample rate | The trigger frequency: the pitch of the note. Each train starts one grain in each period. |
| `energy` | 0 to 1 | The energy of the note over time. Connect an envelope, for example `Env.perc` for a struck sound or `Env.adsr` for a held sound. The levels, the grain durations and the LFO rates follow it. At 0 the output is silent. |
| `velocity` | 0 to 1 | Louder and brighter: the formants become louder faster than the fundamental. At 0 a very soft, nearly pure sound remains. The patterns of the original patch are at about 0.26 to 0.76. |
| `formant1` | 0.25 to 16 | The grain frequency of train 1, as a ratio of `freq`. Whole numbers give harmonic formants. |
| `formant2` | 0.25 to 16 | The grain frequency of train 2, as a ratio of `freq`. |
| `overlap0` | 0 to 4 | The grain duration of train 0, in periods of `freq`. Below 1 there are gaps between the grains, and the sound is brighter. Above 1 the grains overlap. The original patch uses a random value from 0.5 to 2 for each note. |
| `overlap1` | 0 to 4 | The grain duration of train 1, in periods of `freq`. |
| `overlap2` | 0 to 4 | The grain duration of train 2, in periods of `freq`. |
| `wobble` | 0 to 2 | The depth of the LFOs on the grain durations and frequencies. 1 is the depth of the original patch. At 0 there is no wobble. The LFO on the fundamental frequency also moves its mean up by a random amount, up to a quarter of `freq` at 1. |
| `detune` | Hz, 0 to 20 | The largest offset of the trigger frequencies of trains 1 and 2 from `freq`. Each note gets random offsets. The small differences make slow beats between the trains. |
| `jitter` | 0 to 1 | The amount of pink noise on `energy`. At 1, `energy` is multiplied by noise from 0.1 to 1, as in the original patch, which also makes the sound quieter. At 0 there is no noise. |
| `law` | 0 to 1 | How the grain durations follow `energy`. At 0 the grains are short when the energy is high: bright at the start, purer at the end. At 1 the grains are long when the energy is high: pure at the start, buzzy at the end. |
| `width` | 0 to 1 | Stereo width. At 0 the two channels are the same. At 1 they have independent noise and LFO phases. |

## Behaviour to know

- **The envelope is not in the UGen.** Pulsar has no gate and does not free its synth. Put the envelope in the SynthDef, connect it to `energy`, and free the synth with the envelope (`Env.perc(...).kr(Done.freeSelf)`) or with `DetectSilence`.
- **Random values come from the synth.** Each Pulsar draws its random values (LFO phases, trigger offsets, the noise) from the random generator of its synth when it starts, as `Rand` does. Use `RandSeed` to repeat a note exactly.
- **Control rate.** All inputs are read once in each block. `energy`, the noise and the levels change once in each block, and the levels are interpolated over the block, as in a SynthDef with control-rate envelopes. The grains start at the exact sample.
- **A grain holds its values.** A grain keeps its frequency and duration from the sample where it starts, as in GrainSin.
- **Grain pool.** Each train has 16 grains in each channel. From a `freq` of 100 Hz the pool does not get full at any control values; with `wobble`, `detune` and `law` at their defaults this is true from 20 Hz. When a pool is full, a new grain does not start.
- **Aliasing.** A grain frequency above half the sample rate aliases, as in GrainSin. With high notes, keep the formants low.

## Examples

```supercollider
// A struck note. The attack is shorter for high notes, as in the original patch.
(
SynthDef(\pulsar, { |out = 0, freq = 440, amp = 0.2, vel = 0.5, rel = 3|
    var att = freq.clip(50, 1500).linlin(50, 1500, 0.1, 0.02);
    var env = Env.perc(att, rel).kr(Done.freeSelf);
    var sig = Pulsar.ar(freq, env, vel,
        formant1: \formant1.kr(2), formant2: \formant2.kr(6),
        overlap0: Rand(0.5, 2), overlap1: \overlap1.kr(2), overlap2: \overlap2.kr(2));
    Out.ar(out, sig * amp);
}).add;
)

// The pattern of the original patch.
(
Pbind(
    \instrument, \pulsar,
    \dur, Pseq([0.1, 0.5, 1, 2], inf),
    \degree, Pseq([1, 3, 5, 7, 9, 11], inf),
    \octave, Pstutter(3, Pseq((3..5), inf)),
    \overlap1, Pseq(2.pow((-1..2)), inf),
    \overlap2, Pseq(2.pow((-1..1).scramble), inf),
    \formant1, Pseq((1..3), inf),
    \formant2, Pseq((3..7).scramble, inf),
    \vel, Pwhite(0.26, 0.76),
    \rel, Pwhite(1, 7),
    \db, Pgauss(-10, 3)
).play;
)

// A held note: the timbre stays at the sustain level of the envelope.
(
x = { |gate = 1|
    var env = Env.adsr(0.3, 0.5, 0.6, 2).kr(Done.freeSelf, gate);
    Pulsar.ar(110, env, 0.7, formant1: 3, formant2: 7, overlap1: 1, law: 1) * 0.3
}.play;
)
x.set(\gate, 0);

// energy from a slow LFO: the timbre breathes.
{ Pulsar.ar(165, SinOsc.kr(0.2).range(0.1, 0.9), 0.6, formant2: 5) * 0.3 }.play;
```

## References

### Rustamoff: Musical sound design in SuperCollider

Rustamoff, A. *Musical sound design in SuperCollider*, episode 1: pulsar synthesis. Video series on YouTube.

The source of the design. The patch builds a pulsar sound from GrainSin step by step: a train of grains whose duration is tied to the trigger period, a fundamental and two formant trains, an envelope that also controls the grain durations and levels through powers, pink noise on the envelope, and LFOs whose rate follows the envelope. Pulsar implements the SynthDef of the step that adds the LFOs. The later steps add allpass diffusion, which Pulsar does not have; put an effect after it for that. Pulsar keeps the constants of the SynthDef: the powers, the LFO rates and ranges, and the trigger offsets. The controls `law`, `wobble`, `jitter` and `width` turn parts of the patch on and off; `law` 1 is a variant from the same episode.

### Roads 2001

Roads, C. (2001). *Microsound.* MIT Press.

The book that describes pulsar synthesis: a train of short pulses (pulsarets) at a fundamental frequency, each pulse shorter than or as long as its period, so that the pulse length sets a formant independently of the pitch. Pulsar follows this model with sine pulsarets and Hann windows, and allows grains longer than one period (overlap above 1), as GrainSin does.

## Design choices without a source

- The mapping of `velocity` to the level before the powers (0.003 to 0.3 on a log scale) is a choice. The original patch uses a level from 0.01 to 0.1.
- The `width` control, which moves the noise and LFO phases of the right channel from those of the left channel to its own, is a choice. In the original patch the two channels are always independent (width 1).
- The size of the grain pool is a choice that covers the control ranges.
