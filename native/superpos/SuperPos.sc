// SuperPos: overlapping grains from a mono buffer. A read pointer moves
// through the buffer at pointerRate; grains start at the pointer at a rate of
// overlap / grainDur and play the buffer, transposed, with a Hann window.
SuperPos : UGen {
    *ar { |bufnum = 0, pointerRate = 1, grainDur = 0.025, overlap = 5, transpose = 0|
        ^this.multiNew('audio', bufnum, pointerRate, grainDur, overlap, transpose)
    }
}
