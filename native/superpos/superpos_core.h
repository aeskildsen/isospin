// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The DSP core of the SuperPos granular UGen. It uses no SuperCollider
// headers, so that a test program can use it without a server.
//
// SuperPos plays overlapping grains from a mono buffer into two channels. A
// read pointer moves through the buffer; each grain starts at the pointer and
// plays the buffer with a window that the texture control shapes.
//
// With normalize 0 and the other controls at their defaults (regularity 0,
// probability 1, texture 1, and detuneStep, scatter, pan and stereoSpread 0),
// the output is the same as that of this SynthDef graph, sample for sample
// within float rounding:
//
//     trig = Impulse.ar(overlap * grainDur.reciprocal);
//     pos  = Phasor.ar(0, pointerRate * BufRateScale.kr(buf), 0, BufFrames.kr(buf))
//            / BufFrames.kr(buf);
//     GrainBuf.ar(2, trig, grainDur, buf, transpose.midiratio, pos, 2, 0, -1, maxGrains + 1)
//
// To make this true, the core keeps the order of operations and the float and
// double types of these UGens in SuperCollider 3.14.1, and also their
// constructor behaviour (see prime()).
//
// Regularity moves the grain times from a fixed interval (0) to random
// intervals (1). Each interval is the regular interval times
// (1 - regularity) + regularity * E, where E is a random value with an
// exponential distribution and a mean of 1. So the mean interval stays the
// same at every regularity, and at 1 the grain times are a Poisson process,
// as Dust. Probability is the chance that a trigger starts a grain, as
// CoinGate.
//
// Texture sets the window of a grain when the grain starts, as the window
// shape of Mutable Instruments Clouds. Let x be a triangle that goes from 0
// to 1 and back to 0 over the grain. From 0 to 0.5 the window is a trapezoid,
// min(1, x * 0.5 / (texture + 0.01)): steep at 0, near a triangle at 0.5.
// From 0.5 to 1 it goes from the triangle to the Hann window,
// (1 - cos(pi x)) / 2. At 1 (the default) the window is the Hann window of
// GrainBuf.
//
// Each grain also gets random values when it starts, with the ranges of the
// TRand UGens in the SynthDef \cloud that SuperPos comes from: a detune in
// +-detuneStep semitones; a start up to `scatter` seconds after the pointer;
// a pan position in pan +- stereoSpread, folded into [-1, 1]. The pan is
// equal-power, as GrainBuf with two channels.
//
// With normalize on (the default), the output is multiplied by
// 1 / sqrt(n), where n is the number of active grains, smoothed as in the
// granular player of Clouds: the count follows a rise in 0.43 ms and a fall
// in 4.5 ms, and the gain follows its target in 3.1 ms. For grains that are
// not correlated (a noise buffer, or a pointer that does not move with the
// grains) the level then stays the same at all overlaps. With normalize off
// the output is the plain sum of the grains.

#include <cstdint>

namespace superpos {

// One grain. The pool of grains is memory that the caller owns.
struct Grain {
    double phase; // read position in frames
    double rate; // frames per output sample
    double b1, y1, y2; // the sine recurrence of the Hann window
    float amp; // the Hann window value for the next sample
    int counter; // samples that the grain has still to play
    // The window shape, set from the texture when the grain starts.
    double tri, triInc; // 2 * (k + 1) / length at sample k: the triangle phase
    float slope; // > 0: a trapezoid, min(1, triangle * slope)
    float smooth; // with slope 0: triangle + smooth * (Hann - triangle)
    float pan1, pan2; // the gains of the left and the right channel
};

// The random values of the last grain that started, for tests.
struct GrainStart {
    float detune; // semitones
    double scatter; // frames added to the read pointer
    float pan; // -1 to 1, after the fold
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
    float regularity = 0.f; // 0 = a fixed interval, 1 = random intervals; clipped to [0, 1]
    float probability = 1.f; // the chance that a trigger starts a grain; clipped to [0, 1]
    float texture = 1.f; // the window shape (see below); clipped to [0, 1]
    float detuneStep = 0.f; // each grain is detuned by a random value in +-detuneStep semitones
    float scatter = 0.f; // each grain starts a random time in [0, scatter) seconds after the pointer
    float pan = 0.f; // -1 = left, 1 = right
    float stereoSpread = 0.f; // each grain pans to a random value in pan +- stereoSpread; clipped to [0, 1]
    float normalize = 1.f; // >= 0.5 (or NaN): scale the output by 1 / sqrt(active grains); < 0.5: no scaling
};

// A random generator with its own state (Tausworthe, three components, as
// SuperCollider's RGen). Each core has its own, so that more than one server
// can run in one process.
class Random {
public:
    void seed(uint32_t a, uint32_t b, uint32_t c);
    uint32_t next();
    // A value in [0, 1).
    double uniform() { return next() * (1.0 / 4294967296.0); }

private:
    uint32_t s1 = 1243598713u, s2 = 3093459404u, s3 = 1821928721u;
};

class Core {
public:
    // `pool` must hold `maxGrains` grains and stay valid while the core is
    // used. A block has at most `blockSize` samples.
    void init(Grain* pool, int maxGrains, double sampleRate, int blockSize);

    // Sets the random generator. The UGen seeds it from the server's random
    // generator of the synth, so that RandSeed controls it.
    void seed(uint32_t a, uint32_t b, uint32_t c) { mRandom.seed(a, b, c); }

    // Computes the first output sample in the UGen constructor. A grain that
    // starts in it stays and advances one sample. The trigger and the read
    // pointer do not advance: the first block starts again from their
    // initial state. Impulse, Phasor and GrainBuf do the same when they are
    // constructed. The trigger of the first block does not start a second
    // grain, because the trigger was already high in this sample.
    void prime(const BufferView& buf, const Controls& c, float* left, float* right);

    // Writes `n` samples to `left` and `right` (the core writes; it does not
    // add).
    void process(const BufferView& buf, const Controls& c, float* left, float* right, int n);

    int activeGrains() const { return mNumActive; }

    // The number of grains that started since init(), for tests.
    long startedGrains() const { return mStarted; }

    // The random values of the last grain that started, for tests.
    const GrainStart& lastStart() const { return mLastStart; }

private:
    void playActive(const BufferView& buf, float* left, float* right, int n);
    void startGrain(const BufferView& buf, const Controls& c, float pos, double pointer, float* left, float* right,
                    int offset, int n);
    double nextIntervalScale(float regularity);
    void advance(const BufferView& buf, const Controls& c, float* left, float* right, int n, bool keepState);
    void applyLevel(float* left, float* right, int n, bool keepState);
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
    // The phase increment is multiplied by this value until the next
    // trigger: 1 / (the length of the interval in regular intervals).
    double mTrigScale = 1.0;

    // The read pointer in frames, as Phasor.
    double mPointer = 0.0;

    Random mRandom;
    long mStarted = 0;
    GrainStart mLastStart {};

    // The level control: the smoothed grain count, the gain, and the
    // smoothing coefficient of the gain for one sample.
    double mGrainCount = 0.0;
    double mGain = 1.0;
    double mGainCoef = 0.0;
};

} // namespace superpos
