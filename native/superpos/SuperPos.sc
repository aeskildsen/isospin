// SuperPos: overlapping grains from a mono buffer. A read pointer moves
// through the buffer at pointerRate; grains start at the pointer at a rate of
// overlap / grainDur and play the buffer, transposed, with a window.
// regularity moves the grain times from a fixed interval (0) to random
// intervals (1) with the same mean; probability is the chance that a grain
// time starts a grain. texture sets the window of each new grain: a steep
// trapezoid at 0, a triangle at 0.5, a Hann window at 1.
SuperPos : UGen {
    *ar { |bufnum = 0, pointerRate = 1, grainDur = 0.025, overlap = 5, transpose = 0,
            regularity = 0, probability = 1, texture = 1|
        ^this.multiNew('audio', bufnum, pointerRate, grainDur, overlap, transpose,
            regularity, probability, texture)
    }
}
