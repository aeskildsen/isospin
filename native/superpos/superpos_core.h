// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The DSP core of the SuperPos granular UGen. It uses no SuperCollider
// headers, so that a test program can use it without a server.
//
// SuperPos plays overlapping grains from a mono buffer. A read pointer moves
// through the buffer; each grain starts at the pointer and plays the buffer
// with a Hann window.
//
// The output is the same as that of this SynthDef graph, sample for sample
// within float rounding:
//
//     trig = Impulse.ar(overlap * grainDur.reciprocal);
//     pos  = Phasor.ar(0, pointerRate * BufRateScale.kr(buf), 0, BufFrames.kr(buf))
//            / BufFrames.kr(buf);
//     GrainBuf.ar(1, trig, grainDur, buf, transpose.midiratio, pos, 2, 0, -1, maxGrains + 1)
//
// To make this true, the core keeps the order of operations and the float and
// double types of these UGens in SuperCollider 3.14.1, and also their
// constructor behaviour (see prime()).
#pragma once

#include <cstdint>

namespace superpos {

// One grain. The pool of grains is memory that the caller owns.
struct Grain {
    double phase; // read position in frames
    double rate; // frames per output sample
    double b1, y1, y2; // the sine recurrence of the Hann window
    float amp; // the window value for the next sample
    int counter; // samples that the grain has still to play
};

// A view of the buffer that the grains read. `data` is null when there is no
// buffer. Only a buffer with one channel makes grains.
struct BufferView {
    const float* data;
    uint32_t frames;
    uint32_t channels;
    double sampleRate;
};

// The controls. They are read once in each block.
struct Controls {
    float pointerRate; // the speed of the read pointer, 1 = the speed of the buffer
    float grainDur; // seconds
    float overlap; // grains at the same time, on average
    float transpose; // semitones
};

class Core {
public:
    // `pool` must hold `maxGrains` grains and stay valid while the core is
    // used. A block has at most `blockSize` samples.
    void init(Grain* pool, int maxGrains, double sampleRate, int blockSize);

    // Computes the first output sample in the UGen constructor. A grain that
    // starts in it stays and advances one sample. The trigger and the read
    // pointer do not advance: the first block starts again from their
    // initial state. Impulse, Phasor and GrainBuf do the same when they are
    // constructed. The trigger of the first block does not start a second
    // grain, because the trigger was already high in this sample.
    void prime(const BufferView& buf, const Controls& c, float* out);

    // Writes `n` samples to `out` (the core writes; it does not add).
    void process(const BufferView& buf, const Controls& c, float* out, int n);

    int activeGrains() const { return mNumActive; }

private:
    void playActive(const BufferView& buf, float* out, int n);
    void startGrain(const BufferView& buf, const Controls& c, float pos, float* out, int offset, int n);
    void advance(const BufferView& buf, const Controls& c, float* out, int n, bool keepState);
    static bool usable(const BufferView& buf);

    Grain* mPool = nullptr;
    int mMaxGrains = 0;
    int mNumActive = 0;
    double mSampleRate = 0.0;
    double mSampleDur = 0.0;
    double mSlopeFactor = 0.0; // 1 / block size, as SuperCollider's CALCSLOPE

    // The trigger: a wrapping phase, as Impulse.
    double mTrigPhase = 1.0;
    double mTrigInc = 0.0;
    bool mTrigIncSet = false;
    float mPrevTrig = 0.f;

    // The read pointer in frames, as Phasor.
    double mPointer = 0.0;
};

} // namespace superpos
