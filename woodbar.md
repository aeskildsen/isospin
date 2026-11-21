---
summary: Struck wooden bar (modal model with the wood damping law)
categories: UGens>Physical modelling
---

## Description

A physical model of a struck wooden bar: claves, a wooden stick, a xylophone bar or a marimba bar.

The model has three steps:

1. A mallet hits the bar. The model makes a short force pulse. The pulse length comes from the contact of a mallet with a stiffness and a speed. A hard and fast strike gives a short pulse, thus a bright sound.
2. The pulse goes into 8 resonant filters in parallel. Each filter is one mode of the bar. Each mode has a frequency, a decay time and a level.
3. The modes go to two pickups at the two ends of the bar (left and right output). A resonator tube can add more sound at the fundamental.

The decay times are the most important part of the wood sound. Wood has high internal friction. High modes lose their energy much faster than low modes. The arguments `decay` and `tilt` control this, from a dry knock to ringing rosewood and to metal.

The UGen has one voice and two outputs. Use multichannel expansion for more bars. The strike starts when `gate` changes from 0 to 1.

<!-- md2misc: classmethods -->

## Examples

```supercollider
// Three bars: uniform bar (claves), xylophone and marimba tuning.
(
{
    var trig = Impulse.kr(3);
    var bars = IsoWoodbar.ar(
        freq: [220, 330, 440],
        gain: TRand.kr(0.4, 1, trig),
        gate: Trig.kr(trig, 0.01),
        undercut: [0, 0.24, 1],
        hardness: LFNoise1.kr(0.5).range(0, 1),
        seed: [1, 2, 3]
    );
    Mix(bars) * 0.5
}.play;
)

// The two damping controls: decay (mouse x) and tilt (mouse y).
(
{
    var trig = Impulse.kr(4);
    IsoWoodbar.ar(freq: 330, gain: 0.8, gate: Trig.kr(trig, 0.01),
        decay: MouseX.kr(0.05, 4, 1), tilt: MouseY.kr(1, 0))
}.play;
)

// One synth for each note.
(
SynthDef(\woodbar, { |out = 0, freq = 440, amp = 0.8, hardness = 0.5|
    var sig = IsoWoodbar.ar(freq: freq, gain: amp, gate: 1, hardness: hardness);
    DetectSilence.ar(sig[0], doneAction: Done.freeSelf);
    Out.ar(out, sig);
}).add;
)
Pbind(\instrument, \woodbar, \degree, Pwhite(0, 7), \dur, 0.25, \hardness, Pwhite(0.2, 0.9)).play;
```

## References

### Chaigne and Doutaut 1997

Chaigne, A. and Doutaut, V. (1997). *Numerical simulations of xylophones. I. Time-domain modeling of the vibrating bars.* Journal of the Acoustical Society of America 101(1).

A time-domain model of a xylophone bar and its mallet, compared with measurements on real bars. It is the main source of this UGen.

- **The wood damping law.** Chaigne and Doutaut measured the decay of each partial of a padauk bar. The decay rate increases with the square of the frequency: `α(f) = a0 + a2·f²` (Eq. 18, p. 8). The UGen calculates the T60 of each mode with this law, `T60 = ln(1000) / α(f)`. `decay` sets a0, and `tilt` sets a2.
- **Values of real woods.** Table I (p. 6) gives the loss values of padauk and rosewood. The default values of `decay` (1.1) and `tilt` (0.42) are the padauk values. The help text of `tilt` also gives the rosewood value.
- **The mallet.** The paper models the mallet as a mass with a Hertz contact, `F = K·d^1.5` (p. 3). The contact is nonlinear: a harder strike gives a shorter contact and a brighter sound. The UGen does not simulate the contact. It calculates the contact time of the same law and uses it as the length of the force pulse.
- **Mallet values.** Table I gives the contact stiffness of a rubber mallet (3.7e7) and a boxwood mallet (1.31e9). These are the two ends of `hardness`. The mallet mass (about 25 g) and the range of strike speeds (0.05 to 1.5 m/s) also come from the paper. `gain` sets the strike speed in this range.

### Aramaki, Besson, Kronland-Martinet and Ystad 2010

Aramaki, M., Besson, M., Kronland-Martinet, R. and Ystad, S. (2010). *Controlling the perceived material in an impact sound synthesizer.* IEEE Transactions on Audio, Speech, and Language Processing 19(2).

A synthesizer of impact sounds with controls for the perceived material, and listening tests on wood, metal and glass.

- **Two damping controls.** Their damping law has two parameters: a global damping and a frequency-relative damping (Eq. 4, pp. 4-6). Listeners put a sound in the category wood, metal or glass mainly from these two values. Thus the UGen has the two controls `decay` (global) and `tilt` (frequency-relative).
- **Difference.** The UGen uses the quadratic law of Chaigne and Doutaut, not the exponential law of Aramaki et al. Aramaki et al. give the quadratic law of wooden bars as the reason for their form.

### Rossing 1982

Rossing, T. D. (1982). *Acoustics of bar percussion instruments.* Percussive Notes.

An overview of the acoustics of bars, marimbas, xylophones and their resonators.

- **Uniform bar.** The modes of a uniform bar with free ends have the frequency ratios `1 : 2.76 : 5.40 : 8.93 ...` (p. 2). These are the mode ratios at `undercut` = 0.
- **Marimba bar.** Figure 2 gives the measured mode frequencies of a marimba bar tuned to E3 (p. 6). Their ratios, `1 : 3.92 : 9.24 : 16.3 : 24.2 : 33.5 : 43.0`, are the mode ratios at `undercut` = 1. The 8th ratio (53) is an extrapolation. `undercut` moves between the two sets of ratios on a log scale.
- **Xylophone.** The first overtone of a xylophone bar is tuned to 3 times the fundamental (p. 8). In the UGen, this occurs at about `undercut` = 0.24.
- **Resonator tube.** A tube closed at one end resonates at 1 and 3 times its fundamental (p. 8). The resonator of `tube` has these two resonances.

### van den Doel, Kry and Pai 2001

van den Doel, K., Kry, P. G. and Pai, D. K. (2001). *FoleyAutomatic: physically-based sound effects for interactive simulation and animation.* Proceedings of SIGGRAPH 2001.

A system of modal sound models for interactive animation.

- **Strike pulse.** They use the force `1 - cos(2πt/τ)` for `0 ≤ t ≤ τ`. They found that the exact shape is not important, and that the length τ gives the impression of hardness (pp. 3-4). The UGen uses this pulse shape. The length τ comes from the Hertz contact of Chaigne and Doutaut.

### Ren, Yeh, Klatzky and Lin 2013

Ren, Z., Yeh, H., Klatzky, R. and Lin, M. C. (2013). *Auditory perception of geometry-invariant material properties.* IEEE Transactions on Visualization and Computer Graphics 19(4).

A study of a material model with two damping parameters (Rayleigh damping), with listening tests on five materials.

- **Why grain.** Of the five materials, wood fitted the ideal damping law worst (p. 8). The authors give the anisotropy and the high damping of wood as the reasons. Thus `grain` adds fixed random offsets to the mode frequencies (up to 3 %) and decay times (up to 50 %). The sizes of the offsets are not from the paper.

### Zavalishin 2018

Zavalishin, V. (2018). *The Art of VA Filter Design.* Native Instruments.

A book on digital models of analogue filters.

- **The mode filter.** Each mode is a topology-preserving state-variable filter from this book. It stays stable and accurate when its frequency or decay time changes during the sound, for example with `mute`. The direct-form filter of the Faust library is less accurate for low modes. The conversion from T60 to the filter Q and the output scale are not from the book.

## Design choices without a source

These parts of the model are calculations or choices of the author of the UGen, not results from the sources above.

- The level of each mode at the strike point (`position`) comes from the standard mode shapes of a free bar (Euler-Bernoulli theory), in an approximate form. The same shapes are used for the marimba tuning, but an undercut bar has slightly different shapes.
- The contact time formula, `τ = 2.94·dmax/v`, is the classical result for a Hertz contact with a rigid surface. The real bar moves during the contact.
- The two pickups of `spread` and the decay times of the tube (0.5 s and 0.3 s) are choices, not measurements.
