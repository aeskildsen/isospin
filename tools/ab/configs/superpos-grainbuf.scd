// IsoSuperPos (b) against the graph of Impulse, Phasor and GrainBuf that
// native/superpos/superpos_core.h names (a). With normalize 0 and the other
// controls at their defaults, the two outputs are the same, bit for bit: the
// null residual must be -inf dB at lag 0.
// The buffer is 0.1 s of a wave with partials at 200, 300, 470 and 710 Hz,
// made with b_gen, so that the config needs no sound file.
(
setup: [
	[\b_alloc, 0, 4800],
	[\b_gen, 0, \sine2, 1 + 4, 20, 1, 30, 0.5, 47, 0.3, 71, 0.2]
],
a: (
	name: "Impulse, Phasor and GrainBuf",
	def: SynthDef(\abGrainBuf, {
		var buf = 0, grainDur = \grainDur.kr(0.05), overlap = \overlap.kr(4);
		var trig = Impulse.ar(overlap / grainDur);
		var pos = Phasor.ar(0, \pointerRate.kr(0.5) * BufRateScale.kr(buf), 0, BufFrames.kr(buf))
			/ BufFrames.kr(buf);
		var sig = GrainBuf.ar(2, trig, grainDur, buf, \transpose.kr(3).midiratio, pos, 2, 0, -1, 64 + 1);
		Out.ar(0, sig * 0.2);
	})
),
b: (
	name: "IsoSuperPos",
	def: SynthDef(\abSuperPos, {
		var sig = IsoSuperPos.ar(0, \pointerRate.kr(0.5), \grainDur.kr(0.05), \overlap.kr(4),
			\transpose.kr(3), normalize: 0, maxGrains: 64);
		Out.ar(0, sig * 0.2);
	})
),
duration: 4,
tail: 0.5
)
