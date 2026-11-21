// IsoPulsar (b) against the GrainSin SynthDef that it comes from (a), with the
// pattern of that SynthDef. Both sides have random values (LFO phases, noise,
// trigger offsets), so the null residual is not meaningful: compare level,
// brightness and stereo correlation, and listen. pulsar-plain.scd is the
// deterministic comparison.
// The SynthDef is \pulsar of Alik Rustamoff's "Musical sound design in
// SuperCollider", episode 1, with the random values of each note (overlapF,
// rel) as arguments, so that both sides get the same values from the pattern.
(
a: (
	name: "GrainSin SynthDef",
	def: SynthDef(\abPulsarGrainSin, {
		var a, b, c, d, e, f, f_, fund, lev;
		lev = \lev.kr(0.03);
		f = \freq.kr(440);
		f_ = f.clip(50, 1500);
		e = Env.perc(f_.linlin(50, 1500, 0.1, 0.02), \rel.kr(3)).kr;
		e = e * PinkNoise.kr(1!2).range(0.1, 1).lag(0.02);
		fund = GrainSin.ar(1, Impulse.ar(f),
			\overlapF.kr(1) * (1.05 - e.pow(1.1)) * SinOsc.ar(e * 6, {Rand(0, 6.28)}.dup).range(0.87, 1.15) / f,
			f * SinOsc.ar(e * 8, {Rand(0, 6.28)}.dup).range(0.9, Rand(1, 1.5)));
		a = GrainSin.ar(1, Impulse.ar(f + Rand(-3, 3)),
			\overlapA.kr(2) * (1.05 - e.pow(1.5)) * SinOsc.ar(e * 6, {Rand(0, 6.28)}.dup).range(0.87, 1.15) / f,
			f * \formantA.kr(2) * SinOsc.ar(e * 7, {Rand(0, 6.28)}.dup).range(0.95, 1.05));
		b = GrainSin.ar(1, Impulse.ar(f + Rand(-3, 3)),
			\overlapB.kr(2) * (1.05 - e.pow(1.1)) * SinOsc.ar(e * 6, {Rand(0, 6.28)}.dup).range(0.87, 1.15) / f,
			f * \formantB.kr(6) * SinOsc.ar(e * 8, {Rand(0, 6.28)}.dup).range(0.95, Rand(1, 1.2)));
		c = Mix([
			fund * (lev * e).pow(0.7),
			a * (lev * e).pow(f_.linlin(50, 1500, 1.1, 1.5)),
			b * (lev * e).pow(f_.linlin(50, 1500, 1.5, 2.5))
		]);
		d = LeakDC.ar(c) * \amp.kr(0.2);
		DetectSilence.ar(d, doneAction: 2);
		Out.ar(0, d);
	})
),
b: (
	name: "IsoPulsar",
	def: SynthDef(\abPulsar, {
		var f = \freq.kr(440);
		var e = Env.perc(f.clip(50, 1500).linlin(50, 1500, 0.1, 0.02), \rel.kr(3)).kr;
		var d = IsoPulsar.ar(f, e, (\lev.kr(0.03) / 0.003).log10 / 2,
			\formantA.kr(2), \formantB.kr(6), \overlapF.kr(1), \overlapA.kr(2), \overlapB.kr(2))
			* \amp.kr(0.2);
		DetectSilence.ar(d, doneAction: 2);
		Out.ar(0, d);
	})
),
events: { |instrument|
	Pbind(
		\instrument, instrument,
		\dur, Pseq([0.1, 0.5, 1, 2], inf),
		\degree, Pseq([1, 3, 5, 7, 9, 11], inf),
		\octave, Pstutter(3, Pseq((3..5), inf)),
		\overlapA, Pseq(2.pow((-1..2)), inf),
		\overlapB, Pseq(2.pow((-1..1).scramble), inf),
		\formantA, Pseq((1..3), inf),
		\formantB, Pseq((3..7).scramble, inf),
		\lev, Pshuf((1..10) * 0.01, inf),
		\overlapF, Pwhite(0.5, 2),
		\rel, Pwhite(1, 7),
		\db, Pgauss(-10, 3)
	)
},
duration: 24,
tail: 8
)
