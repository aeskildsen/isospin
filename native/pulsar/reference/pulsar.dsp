// Pulsar synthesis: three trains of sine grains, with a timbre that follows
// the energy of the note.
//
// This is the Faust version of the Pulsar UGen. It is the reference of the
// design, and it is not in the release: Faust calculates all grain voices in
// each sample, also the silent ones, so it costs several times the CPU of the
// GrainSin SynthDef. The UGen is the C++ core in ../pulsar_core.cpp. One
// difference: here the seed control selects the random values; the UGen
// takes them from the random generator of the synth, as Rand.
//
// After the pulsar SynthDef of Alik Rustamoff's "Musical sound design in
// SuperCollider" series (episode 1, pulsar synthesis), which uses three
// GrainSin UGens. This file makes the same sound in one UGen.
//
// Each train starts a grain at each period of the trigger frequency. A grain
// is a sine at the grain frequency under a Hann window. The grain duration is
// a number of trigger periods (overlap). The three trains are:
// * 0: the fundamental. Grain frequency = freq.
// * 1: the first formant. Grain frequency = freq * formant1.
// * 2: the second formant. Grain frequency = freq * formant2.
//
// The energy input is the energy of the note over time, 0 to 1. Usually it
// is an envelope from the SynthDef, but any control signal can be used. It
// controls the sound in three ways:
// * Level. The level of each train is (lev * energy)^p, where lev comes from
//   velocity (see levelOf). The power p of the formants is larger than that
//   of train 0, and it increases with freq. Thus the formants fade before
//   the fundamental, a high note fades to a pure sound sooner than a low
//   note, and a soft note is darker than a loud note.
// * Grain duration. With law 0, the grains are short when energy is high
//   (bright, buzzy) and become longer as energy falls (a purer sound). law 1
//   gives the opposite.
// * Wobble. Slow sine LFOs move the grain durations and frequencies. Their
//   rate is proportional to energy, thus they slow down and stop at the end
//   of the note.
// Pink noise jitters energy, separately in each channel (jitter).
//
// Stereo: the two channels share the grain triggers. Each channel has its
// own noise and its own LFO phases, and holds its own grain duration and
// frequency at each trigger. width sets how different the two channels are.
//
// The help page is pulsar.md. The build adds the arguments to it: the
// argument texts are the tooltips below, with the ranges of the sliders.
//
// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later

declare name "pulsar";
declare description "Pulsar synthesis: three trains of sine grains with a timbre that follows the energy of the note";
declare license "AGPL-3.0-or-later";

import("stdfaust.lib");

// Grain voices for each train and channel. A grain can last up to N trigger
// periods; a longer grain is made shorter (see grainLen).
N = 5;

freq = hslider("freq[tooltip:Trigger frequency in Hz: the pitch of the note. Each train starts one grain in each period.]", 440, 20, 5000, 0.01);
energy = hslider("energy[tooltip:Energy of the note over time, 0 to 1. Connect an envelope, for example Env.perc for a struck sound or Env.adsr for a held sound. Any control signal can be used. The levels, the grain durations and the LFO rates follow it. At 0 the output is silent.]", 1, 0, 1, 0.0001) : smoothEnergy;
velocity = hslider("velocity[tooltip:Velocity of the note, 0 to 1. A higher velocity is louder and brighter, as on an acoustic instrument: the formants become louder faster than the fundamental. At 0 a very soft, nearly pure sound remains. The original patterns are at about 0.26 to 0.76.]", 0.5, 0, 1, 0.001);
formant1 = hslider("formant1[tooltip:Grain frequency of train 1 as a ratio of freq. Whole numbers give harmonic formants.]", 2, 0.25, 16, 0.001);
formant2 = hslider("formant2[tooltip:Grain frequency of train 2 as a ratio of freq.]", 6, 0.25, 16, 0.001);
overlap0 = hslider("overlap0[tooltip:Grain duration of the fundamental train 0, in trigger periods. Below 1 there are gaps between the grains, and the sound is brighter. Above 1 the grains overlap. The original patch uses a random value from 0.5 to 2 for each note.]", 1, 0, 4, 0.001);
overlap1 = hslider("overlap1[tooltip:Grain duration of train 1, in trigger periods. A short grain gives a wide, buzzy formant. A long grain gives a narrow formant, near a pure sine at freq*formant1.]", 2, 0, 4, 0.001);
overlap2 = hslider("overlap2[tooltip:Grain duration of train 2, in trigger periods.]", 2, 0, 4, 0.001);
wobble = hslider("wobble[tooltip:Depth of the LFOs on the grain durations and frequencies. 1 is the depth of the original patch. At 0 there is no wobble. The LFO on the fundamental frequency also shifts its mean up by a random amount.]", 1, 0, 2, 0.001);
detune = hslider("detune[tooltip:Maximum offset in Hz of the trigger frequencies of trains 1 and 2 from freq. seed selects the offsets. The small differences make slow beats between the trains.]", 3, 0, 20, 0.01);
jitter = hslider("jitter[tooltip:Amount of pink noise on energy. At 1, energy is multiplied by noise from 0.1 to 1, as in the original patch. This also makes the sound quieter. At 0 there is no noise.]", 1, 0, 1, 0.001);
law = hslider("law[tooltip:How the grain durations follow energy. At 0 the grains are short when energy is high: bright at the start, purer at the end. At 1 the grains are long when energy is high: pure at the start, buzzy at the end.]", 0, 0, 1, 0.001);
width = hslider("width[tooltip:Stereo width. At 0 both channels are the same. At 1 they have independent noise and LFO phases.]", 1, 0, 1, 0.001);
seed = hslider("seed[tooltip:Selects the random values of the note: LFO phases, trigger offsets, the mean shift of the fundamental and the noise. Give each note a different value, for example Rand(0, 1000).round in the SynthDef. Use whole numbers.]", 0, 0, 1000, 1);

//------------------------------- helpers --------------------------------------

// velocity 0 to 1 to the level before the power curves, 0.003 to 0.3 on a
// log scale. The original patterns use levels of 0.01 to 0.1 (velocity 0.26
// to 0.76). Velocity 0.5 gives 0.03.
levelOf(v) = 0.003*pow(100, v);
lev = levelOf(velocity);

// The energy slider changes once per block. Smoothing removes the steps.
// The output starts at the first value (see smooth in wood.lib).
smoothEnergy(x) = x0 + (x - x0 : si.smooth(ba.tau2pole(0.001)))
with { x0 = ba.sAndH(1 - 1', x); };

// A fixed pseudo-random number in [0, 1) for an index and the seed.
rnd(i) = ma.frac(sin(12.9898*(i + 1) + 78.233*seed)*43758.5453);

// Sine table. sinT(p) = sin(2*pi*p) for p in [0, 1).
TS = 1 << 16;
sinT(p) = rdtable(TS, sin(2*ma.PI*float(ba.time)/TS), int(p*TS));

// A trigger at sample 0 and at each period of rate, as Impulse.ar.
impulse(rate) = (ph < ph') | (1 - 1')
with { ph = (+(rate/ma.SR) : ma.frac) ~ _; };

// A sine LFO with a rate in Hz and a start phase in cycles.
lfo(rate, ph0) = sinT(ma.frac(acc + ph0))
with { acc = (+(rate/ma.SR) : ma.frac) ~ _; };

// White noise for channel k and the seed, in [-1, 1].
noise(k) = no.noise_env(12345 + 7919*k + 104729*int(seed)).noise;

// Pink noise, lagged as Lag.kr(0.02) and scaled to the spread of
// PinkNoise.kr(1).lag(0.02) in SuperCollider (standard deviation 0.125).
lagPole = exp(log(0.001)/(0.02*ma.SR));
pink(k) = noise(k) : no.pink_filter : si.smooth(lagPole) : *(4.94);

// SuperCollider's LeakDC.ar with coefficient 0.995.
leakDC = fi.pole(0.995) : fi.zero(1);

//-------------------------------- grains --------------------------------------

// The grain length in samples: as GrainSin, at least 4. It is at most N
// trigger periods less one sample, thus a voice always stops before it gets
// its next grain.
grainLen(dur, rate) = max(4, min(floor(dur*ma.SR), floor(N*ma.SR/rate) - 1));

// One grain voice. tr starts a grain: the voice holds the frequency and the
// length, and plays sin(2*pi*f*n/SR) under the Hann window
// sin^2(pi*(n + 1)/len), for n = 0 to len - 1, as GrainSin.
voice(tr, f, len) = (n < L)*win*sinT(ma.frac(n*inc))
with {
  L = ba.sAndH(tr, len);
  inc = ba.sAndH(tr, f/ma.SR);
  winc = ba.sAndH(tr, 1/len);
  n = ba.countup(1 << 30, tr);
  win = 0.5*(1 - sinT(ma.frac((n + 1)*winc + 0.25)));
};

// One train in one channel: N voices that take the triggers in turn.
train(trig, f, len) = par(v, N, voice(trig*(cnt == v), f, len)) :> _
with { cnt = (\(c).((c + trig) % N)) ~ _; };

//------------------------------- one channel ----------------------------------

// The trigger frequencies of the three trains (shared by both channels).
rate0 = freq;
rate1 = max(1, freq + detune*(2*rnd(1) - 1));
rate2 = max(1, freq + detune*(2*rnd(2) - 1));
trig0 = impulse(rate0);
trig1 = impulse(rate1);
trig2 = impulse(rate2);

// The upper ends of the LFO ranges of the frequencies of trains 0 and 2 (the original
// patch uses Rand(1, 1.5) and Rand(1, 1.2)).
top0 = 1 + 0.5*rnd(3);
top2 = 1 + 0.2*rnd(4);

// The power of each train's level: train 0 0.7; train 1 1.1 to 1.5 and train 2 1.5 to 2.5
// over freq 50 to 1500 Hz.
u = (min(1500, max(50, freq)) - 50)/1450;
p1 = 1.1 + 0.4*u;
p2 = 1.5 + u;

// LFO from lo to hi, scaled by wobble towards 1.
range(lo, hi, s) = 1 + wobble*(0.5*(lo + hi) - 1 + 0.5*(hi - lo)*s);

// The duration law: 1.05 - e^p at law 0, 0.05 + e^p at law 1.
durLaw(e, p) = 1.05 - ep + law*(2*ep - 1)
with { ep = pow(e, p); };

// One channel. n is the jitter noise, ph(j) the six LFO start phases.
channel(n, ph) = leakDC(g0*s0 + g1*s1 + g2*s2)
with {
  e = energy*max(0, 1 - jitter*(0.45 - 0.45*n));
  s0 = train(trig0, freq*range(0.9, top0, lfo(e*8, ph(1))),
             grainLen(overlap0*durLaw(e, 1.1)*range(0.87, 1.15, lfo(e*6, ph(0)))/freq, rate0));
  s1 = train(trig1, freq*formant1*range(0.95, 1.05, lfo(e*7, ph(3))),
             grainLen(overlap1*durLaw(e, 1.5)*range(0.87, 1.15, lfo(e*6, ph(2)))/freq, rate1));
  s2 = train(trig2, freq*formant2*range(0.95, top2, lfo(e*8, ph(5))),
             grainLen(overlap2*durLaw(e, 1.1)*range(0.87, 1.15, lfo(e*6, ph(4)))/freq, rate2));
  le = lev*e;
  g0 = pow(le, 0.7);
  g1 = pow(le, p1);
  g2 = pow(le, p2);
};

//--------------------------------- stereo -------------------------------------

// The left channel uses its own phases and noise. The right channel moves
// from those of the left channel (width 0) to its own (width 1).
phL(j) = rnd(10 + j);
phR(j) = phL(j) + width*(rnd(20 + j) - phL(j));
theta = width*ma.PI/2;
noiseL = pink(0);
noiseR = noiseL*cos(theta) + pink(1)*sin(theta);

process = channel(noiseL, phL), channel(noiseR, phR);
