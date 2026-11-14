// SuperPos: overlapping grains from a mono buffer, in two channels (left,
// right). A read pointer moves through the buffer at pointerRate; grains
// start at the pointer at a rate of overlap / grainDur and play the buffer,
// transposed, with a window.
// regularity moves the grain times from a fixed interval (0) to random
// intervals (1) with the same mean; probability is the chance that a grain
// time starts a grain. texture sets the window of each new grain: a steep
// trapezoid at 0, a triangle at 0.5, a Hann window at 1.
// Each grain gets a random detune in +-detuneStep semitones, a random start
// up to scatter seconds after the pointer, and a random pan position in
// pan +- stereoSpread. normalize (on by default) scales the output by
// 1 / sqrt(active grains), so that the level stays the same at all overlaps;
// set it to 0 for the plain sum of the grains.
SuperPos : MultiOutUGen {
    *ar { |bufnum = 0, pointerRate = 1, grainDur = 0.025, overlap = 5, transpose = 0,
            regularity = 0, probability = 1, texture = 1,
            detuneStep = 0, scatter = 0, pan = 0, stereoSpread = 0, normalize = 1|
        ^this.multiNew('audio', bufnum, pointerRate, grainDur, overlap, transpose,
            regularity, probability, texture, detuneStep, scatter, pan, stereoSpread, normalize)
    }

    init { |... theInputs|
        inputs = theInputs;
        ^this.initOutputs(2, rate)
    }
}
