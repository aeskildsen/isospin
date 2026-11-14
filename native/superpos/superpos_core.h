// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The DSP core of the SuperPos granular UGens. It uses no SuperCollider
// headers, so that a test program can use it without a server.
//
// Engine<Source> plays overlapping grains from a mono buffer into two
// channels, with a window that the texture control shapes. The source says
// where in the buffer each grain starts (see FileSource for the interface).
// SuperPos uses FileSource: a read pointer moves through the buffer, and each
// grain starts at the pointer. Engine<FileSource> is also called Core.
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

// The controls of the engine. They are read once in each block. The controls
// of a source are in the source.
struct Controls {
    float grainDur; // seconds
    float overlap; // grains at the same time, on average
    float transpose; // semitones
    float regularity = 0.f; // 0 = a fixed interval, 1 = random intervals; clipped to [0, 1]
    float probability = 1.f; // the chance that a trigger starts a grain; clipped to [0, 1]
    float texture = 1.f; // the window shape (see below); clipped to [0, 1]
    float detuneStep = 0.f; // each grain is detuned by a random value in +-detuneStep semitones
    float scatter = 0.f; // each grain starts a random time in [0, scatter) seconds from its place (see the source)
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

// The file source: a read pointer moves through the buffer as Phasor, from
// 0 to the end of the buffer and again from 0. A grain starts at the pointer,
// or `scatter` frames after it.
//
// The interface of a source, which Engine calls:
//   reset(blockSize)        at init. A block has at most blockSize samples.
//   begin(buf, sampleDur, n)
//                           once in each block of n samples, before the
//                           grains play.
//   tick()                  once in each sample, before a grain can start in
//                           that sample.
//   place(buf, scattered, scatterFrames, rate, length)
//                           the start phase in frames of a grain that starts
//                           in this sample, or NaN for no grain. `rate` is
//                           the read speed in frames per sample, `length` the
//                           grain length in samples.
//   end(keepState)          after the block. With keepState false (the
//                           constructor sample), the state goes back to that
//                           before begin().
class FileSource {
public:
    // The speed of the read pointer, 1 = the speed of the buffer. Read in
    // begin().
    void setPointerRate(float rate) { mPointerRate = rate; }

    void reset(int) { mPointer = 0.0; }

    void begin(const BufferView& buf, double sampleDur, int n);

    void tick() {
        mWork = wrapPointer(mWork);
        mPos = static_cast<float>(mWork) / mFrames;
        mGrainPointer = mWork;
        mWork += mRate;
    }

    double place(const BufferView& buf, bool scattered, double scatterFrames, double rate, double length) const;

    void end(bool keepState) {
        if (keepState)
            mPointer = mWork;
    }

private:
    double wrapPointer(double pointer) const;

    float mPointerRate = 1.f;
    double mPointer = 0.0; // in frames, between blocks
    double mWork = 0.0; // the pointer during a block
    double mRate = 0.0; // frames per sample in this block
    float mFrames = 0.f;
    float mPos = 0.f; // the pointer as a part of the buffer length, this sample
    double mGrainPointer = 0.0; // the pointer in frames, this sample
};

template <class Source> class Engine {
public:
    // `pool` must hold `maxGrains` grains and stay valid while the engine is
    // used. A block has at most `blockSize` samples.
    void init(Grain* pool, int maxGrains, double sampleRate, int blockSize);

    // Sets the random generator. The UGen seeds it from the server's random
    // generator of the synth, so that RandSeed controls it.
    void seed(uint32_t a, uint32_t b, uint32_t c) { mRandom.seed(a, b, c); }

    // The source, to set its controls before prime() or process().
    Source& source() { return mSource; }

    // Computes the first output sample in the UGen constructor. A grain that
    // starts in it stays and advances one sample. The trigger and the source
    // do not advance: the first block starts again from their initial state.
    // Impulse, Phasor and GrainBuf do the same when they are constructed. The
    // trigger of the first block does not start a second grain, because the
    // trigger was already high in this sample.
    void prime(const BufferView& buf, const Controls& c, float* left, float* right);

    // Writes `n` samples to `left` and `right` (the engine writes; it does
    // not add).
    void process(const BufferView& buf, const Controls& c, float* left, float* right, int n);

    int activeGrains() const { return mNumActive; }

    // The number of grains that started since init(), for tests.
    long startedGrains() const { return mStarted; }

    // The random values of the last grain that started, for tests.
    const GrainStart& lastStart() const { return mLastStart; }

private:
    void playActive(const BufferView& buf, float* left, float* right, int n);
    void startGrain(const BufferView& buf, const Controls& c, float* left, float* right, int offset, int n);
    double nextIntervalScale(float regularity);
    void advance(const BufferView& buf, const Controls& c, float* left, float* right, int n, bool keepState);
    void applyLevel(float* left, float* right, int n, bool keepState);

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

    Source mSource;
    Random mRandom;
    long mStarted = 0;
    GrainStart mLastStart {};

    // The level control: the smoothed grain count, the gain, and the
    // smoothing coefficient of the gain for one sample.
    double mGrainCount = 0.0;
    double mGain = 1.0;
    double mGainCoef = 0.0;
};

// The live source: the source writes its input into the buffer, which is a
// circular record buffer. The record head is where the next input sample
// goes. A grain starts `position` (0 to 1, a part of the buffer length) plus
// `scatter` frames behind the head. `freeze` stops the recording: the head
// stays and the buffer keeps its content.
//
// A grain must not read across the record head. Over the grain, the distance
// from the head to the read position changes by (head speed - rate) frames in
// each sample, where the head speed is 1 (0 when frozen). The source writes
// the input of a whole block before the grains play, so the frames up to one
// block ahead of the head already hold input that comes later in the block.
// place() moves the start so that the distance stays in
// [kMinDistance, frames - kMinDistance - blockSize] for the whole grain, or
// gives NaN (no grain) when the grain does not fit in the buffer. The limit
// holds while freeze does not change during the grain.
class LiveSource {
public:
    // The distance from the head, in frames, that a read stays away from. 2:
    // linear interpolation reads the frame after the read position too.
    static constexpr double kMinDistance = 2.0;

    // The controls of a block. `in` holds the input samples; with `inStep` 0
    // it holds one value for the whole block (an input that is not at audio
    // rate). `data` is the buffer to write, the same memory as the BufferView
    // of the block; null: no recording.
    void setInput(const float* in, int inStep, float* data, float freeze, float position);

    void reset(int blockSize) {
        mHead = 0;
        mBlockSize = blockSize;
    }

    void begin(const BufferView& buf, double sampleDur, int n);

    void tick() { ++mTick; }

    double place(const BufferView& buf, bool scattered, double scatterFrames, double rate, double length) const;

    void end(bool keepState) {
        if (keepState)
            mHead = mHeadEnd;
    }

    // The record head in frames, for tests.
    uint32_t head() const { return mHead; }

private:
    const float* mIn = nullptr;
    int mInStep = 0;
    float* mData = nullptr;
    bool mFrozen = false;
    double mPosition = 0.0;

    int mBlockSize = 0;
    uint32_t mHead = 0; // between blocks
    uint32_t mHeadStart = 0; // the head at the start of this block
    uint32_t mHeadEnd = 0; // the head at the end of this block
    int mTick = -1; // the sample of this block: tick() runs before place()
};

// The engine of SuperPos.
using Core = Engine<FileSource>;

// The engine of SuperPosLive.
using LiveCore = Engine<LiveSource>;

// True when a buffer can make grains: it has data, one channel and frames.
bool usableBuffer(const BufferView& buf);

} // namespace superpos
