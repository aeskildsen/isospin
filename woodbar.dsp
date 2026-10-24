// A struck wooden bar: claves, wood stick, xylophone or marimba bar.
//
// Modal bar (8 modes) with the wood damping law and a Hertz-timed mallet. The
// sources are named in wood.lib.
// * decay, tilt: the two damping controls (t60law in wood.lib). They move the
//   sound from ringing rosewood to a dead "thuck", or with a low tilt, to
//   metal.
// * undercut: mode ratios from the uniform bar (0, claves, sticks) to the
//   tuned marimba bar (1). The xylophone is at about 0.24.
// * hardness: mallet stiffness from rubber to boxwood. Velocity also shortens
//   the contact, as a real mallet does.
// * position: strike point from the bar end (0) to the middle (0.5). The
//   middle does not excite the antisymmetric modes.
// * grain: random offsets of the upper mode frequencies and decays, for
//   natural variation between bars. seed selects the set of offsets.
// * tube: level of a closed resonator tube tuned to freq (resonances at 1 and
//   3 times freq).
// * mute: how much the bar is damped when the note is released.
// * spread: two pickups near the two bar ends. The antisymmetric modes have
//   opposite phase at the two ends, and spread sets how much of this the
//   stereo output keeps.
//
// The argument texts are the tooltips below.
//
// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later

declare name "woodbar";
declare description "Struck wooden bar with wood damping law";
declare nvoices "16";
declare license "AGPL-3.0-or-later";

import("stdfaust.lib");
wood = library("wood.lib");

N = 8;

freq = hslider("freq[tooltip:Frequency of the fundamental in Hz. A marimba goes from about 65 to 2100 Hz, a xylophone from about 350 to 4200 Hz. Below about 65 Hz the bar becomes a low thud. At high frequencies the upper modes go above the audio band and are removed, thus fewer modes remain.]", 440, 20, 5000, 0.01);
gain = hslider("gain[tooltip:Strike velocity. A harder strike is louder, and its shorter mallet contact makes it brighter. At 0 a very soft strike remains.]", 0.7, 0, 1, 0.01);
gate = button("gate[tooltip:A change from 0 to 1 strikes the bar. A change to 0 releases the note (see mute). Use Trig.kr or a gate signal with the values 0 and 1.]");

decay = hslider("decay[tooltip:Decay time (T60) of the low modes in seconds. The higher modes decay faster, as tilt sets. Padauk is about 1.1, rosewood about 0.27. Below about 0.1 the strike is a short, dry knock. Above about 3 the bar rings like metal or glass.]", 1.1, 0.02, 6, 0.01);
tilt = hslider("tilt[tooltip:How much faster the high modes decay than the low modes. Padauk is about 0.42, rosewood about 0.56. Below about 0.3 the high modes ring nearly as long as the low ones, and the bar sounds like metal. Above about 0.7 the high modes stop in a few milliseconds, and the bar sounds dull and damp.]", 0.42, 0, 1, 0.01);
undercut = hslider("undercut[tooltip:Tuning of the modes. At 0 the modes are those of a uniform bar (claves, a stick, inharmonic). At 1 they are those of a tuned marimba bar. A xylophone is at about 0.24.]", 1, 0, 1, 0.01);
hardness = hslider("hardness[tooltip:Stiffness of the mallet, from rubber (0, soft and dark) to boxwood (1, hard and bright).]", 0.5, 0, 1, 0.01);
position = hslider("position[tooltip:Strike point, from the end of the bar (0, all modes, bright) to the middle (0.5, the antisymmetric modes stop, round).]", 0.25, 0, 0.5, 0.01);
grain = hslider("grain[tooltip:Random offsets of the upper mode frequencies and decays, as in real wood. At 0 the modes follow the ideal law. Above about 0.5 the upper modes are audibly out of tune with each other.]", 0.2, 0, 1, 0.01);
seed = hslider("seed[tooltip:Selects the set of random offsets for grain. Give each instance a different value, for example in multichannel expansion. Use whole numbers.]", 0, 0, 100, 1);
tube = hslider("tube[tooltip:Level of a resonator tube under the bar, tuned to freq and 3 times freq. It makes the fundamental louder and longer.]", 0, 0, 1, 0.01) : wood.smooth;
mute = hslider("mute[tooltip:Damping when gate goes to 0, as a hand on the bar. At 0 the bar rings on after the release. At 1 it stops in a few tens of milliseconds.]", 0, 0, 1, 0.01);
spread = hslider("spread[tooltip:Stereo width. Two pickups at the ends of the bar receive the antisymmetric modes in opposite phase. At 0 the output is mono. Near 1 the mono sum loses those modes.]", 0.3, 0, 1, 0.01) : wood.smooth;

trig = gate > gate';
exc = wood.strike(0.025, hardness, wood.vel(gain), trig);

// Damping on note-off: the T60 of all modes falls to 3 % at mute = 1. A step
// in the damping does not click, thus it is not smoothed.
release = 1 - 0.97*mute*(gate == 0);

fm(i) = freq*wood.barRatio(undercut, i)*(1 + 0.03*grain*wood.rnd(i, seed)*(i > 0));
tm(i) = wood.t60law(decay, tilt, fm(i))*(1 + 0.5*grain*wood.rnd(i + 50, seed))*release;
// The strike position changes only the mode gains, thus only the gains are
// smoothed.
gm(i) = wood.barShape(i, position) : wood.smooth;

modes = exc <: par(i, N, wood.mode(fm(i), tm(i), gm(i)));
pickups = par(i, N, _ <: _, *(1 - 2*spread*(i%2))) :> _, _;

// A closed tube resonates at 1 and 3 times its fundamental (Rossing 1982 p8).
// tubeMode has a gain of 1 at its resonance frequency.
tubeMode(f, t60) = wood.mode(f, t60, 2*wood.LN1000/(t60*ma.SR));
resonator = _ <: tubeMode(freq, 0.5), 0.5*tubeMode(3*freq, 0.3) :> *(2*tube);
withTube = _ <: _ + resonator;

process = modes : pickups : par(c, 2, withTube : *(0.4));
