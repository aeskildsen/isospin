// IsoPulsar (b) against three GrainSin trains (a), without random parts: no
// wobble, jitter or detune, and a constant energy. The two sides then make
// the same grains. Expect a null residual of about -30 to -40 dB: a grain
// length can differ by one sample, because GrainSin calculates the duration
// in single precision. Set AB_FREQ to try another pitch (default 220 Hz).
(
var freq = ("AB_FREQ".getenv ? "220").asFloat;
var energy = 0.5, lev = 0.05;
(
a: (
	name: "GrainSin trains",
	def: SynthDef(\abPulsarPlainGrainSin, {
		var f = freq, e = DC.kr(energy), f_ = f.clip(50, 1500);
		var fund = GrainSin.ar(1, Impulse.ar(f), 1 * (1.05 - e.pow(1.1)) / f, f);
		var a = GrainSin.ar(1, Impulse.ar(f), 2 * (1.05 - e.pow(1.5)) / f, f * 2);
		var b = GrainSin.ar(1, Impulse.ar(f), 2 * (1.05 - e.pow(1.1)) / f, f * 6);
		var c = Mix([
			fund * (lev * e).pow(0.7),
			a * (lev * e).pow(f_.linlin(50, 1500, 1.1, 1.5)),
			b * (lev * e).pow(f_.linlin(50, 1500, 1.5, 2.5))
		]);
		Out.ar(0, LeakDC.ar(c) ! 2);
	})
),
b: (
	name: "IsoPulsar",
	def: SynthDef(\abPulsarPlain, {
		Out.ar(0, IsoPulsar.ar(freq, energy, (lev / 0.003).log10 / 2, 2, 6, 1, 2, 2,
			wobble: 0, detune: 0, jitter: 0, width: 0));
	})
),
duration: 2,
tail: 0
)
)
