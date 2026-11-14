// SuperPosLive: the grains of SuperPos from a circular record buffer. The
// UGen writes `in` into `bufnum` (a mono Buffer from the language; its length
// is the longest delay) and plays grains of the recent input, in two
// channels (left, right).
// `in` can be any signal: a bus (In.ar), another UGen, or a device input
// (SoundIn). freeze (>= 0.5) stops the recording and keeps the buffer.
// position sets where grains start: 0 = the newest input, 1 = the oldest. A
// grain never reads across the record head; when it cannot stay behind the
// head (a long grain transposed up in a short buffer), it does not start.
// The other arguments are those of SuperPos; scatter moves a grain further
// back.
SuperPosLive : MultiOutUGen {
    *ar { |in = 0, bufnum = 0, freeze = 0, position = 0, grainDur = 0.025, overlap = 5, transpose = 0,
            regularity = 0, probability = 1, texture = 1,
            detuneStep = 0, scatter = 0, pan = 0, stereoSpread = 0, normalize = 1|
        ^this.multiNew('audio', bufnum, in, freeze, position, grainDur, overlap, transpose,
            regularity, probability, texture, detuneStep, scatter, pan, stereoSpread, normalize)
    }

    init { |... theInputs|
        inputs = theInputs;
        ^this.initOutputs(2, rate)
    }
}
