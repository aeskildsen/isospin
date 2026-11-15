// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later AND GPL-3.0-or-later AND MIT
//
// Parts of this file are adapted from other projects:
//
// SuperCollider 3.14.1 (https://github.com/supercollider/supercollider):
// wrap() (sc_wrap), loopPosition() (sc_gloop), wrapTrigger() (Impulse),
// fold() (sc_fold), Random (RGen), and the grain loop, the Hann window
// recurrence and the pan law of GrainBuf (server/plugins/GrainUGens.cpp).
//     Copyright (c) 2002 James McCartney. All rights reserved.
//     GrainUGens.cpp also says: "Created by Josh Parmenter on 2/4/05.
//     Copyright 2005 __MyCompanyName__. All rights reserved."
//     These files are free software under the GNU General Public License,
//     version 2 or (at your option) any later version. This file uses them
//     under version 3 or (at your option) any later version, which permits
//     their combination with the GNU Affero General Public License version 3
//     (section 13 of each licence).
//
// Mutable Instruments Clouds (https://github.com/pichenettes/eurorack,
// clouds/dsp/grain.h and clouds/dsp/granular_sample_player.h): the window
// shape of setWindow() and windowGain(), and the time constants of the level
// control.
//     Copyright 2014 Emilie Gillet.
//
//     Permission is hereby granted, free of charge, to any person obtaining a
//     copy of this software and associated documentation files (the
//     "Software"), to deal in the Software without restriction, including
//     without limitation the rights to use, copy, modify, merge, publish,
//     distribute, sublicense, and/or sell copies of the Software, and to
//     permit persons to whom the Software is furnished to do so, subject to
//     the following conditions:
//
//     The above copyright notice and this permission notice shall be included
//     in all copies or substantial portions of the Software.
//
//     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//     OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//     MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//     IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//     CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//     TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//     SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#include "superpos_core.h"

#include <algorithm>
#include <cmath>

namespace superpos {

namespace {

// The helpers of the per-sample grain loop must be inlined into it. GCC at
// -O2 does not inline them into both versions of playGrainWith by itself.
#if defined(__GNUC__) || defined(__clang__)
#define SUPERPOS_INLINE inline __attribute__((always_inline))
#else
#define SUPERPOS_INLINE inline
#endif

// constexpr: SuperSonic's web build does not run the dynamic initialisation
// of namespace-scope variables, so std::acos(-1.) here would stay 0.
constexpr double kPi = 3.141592653589793238462643383279502884;
// pi / 2 in float, as pi2_f of SuperCollider: float(pi) * 0.5f.
constexpr float kHalfPiF = static_cast<float>(kPi) * 0.5f;

// Wraps `in` into [lo, hi), as sc_wrap of SuperCollider (double version).
double wrap(double in, double lo, double hi) {
    double range;
    if (in >= hi) {
        range = hi - lo;
        in -= range;
        if (in < hi)
            return in;
    } else if (in < lo) {
        range = hi - lo;
        in += range;
        if (in >= lo)
            return in;
    } else {
        return in;
    }
    if (hi == lo)
        return lo;
    return in - range * std::floor((in - lo) / range);
}

// Wraps a read position into [0, hi), as sc_gloop of GrainBuf, but the result
// is always in [0, hi).
SUPERPOS_INLINE double loopPosition(double in, double hi) {
    if (in >= hi) {
        in -= hi;
        if (in < hi)
            return in;
    } else if (in < 0.) {
        in += hi;
        // A very small negative position plus hi can round to hi. sc_gloop
        // returns it; here the general case below makes it 0.
        if (in >= 0. && in < hi)
            return in;
    } else {
        return in;
    }
    in -= hi * std::floor(in / hi);
    // For a very large position the result can be rounded to hi, or be NaN.
    // Both would read outside the buffer.
    if (!(in >= 0. && in < hi))
        return 0.;
    return in;
}

// The trigger output for the current phase. When the phase passes 1 (or 0
// for a negative increment), it wraps and the trigger is 1. As Impulse.
float wrapTrigger(double inc, double& phase) {
    if (inc < 0.f) {
        if (phase <= 0.f) {
            phase += 1.f;
            if (phase <= 0.f)
                phase -= std::ceil(phase);
            return 1.f;
        }
        return 0.f;
    }
    if (phase >= 1.f) {
        phase -= 1.f;
        if (phase >= 1.f)
            phase -= std::floor(phase);
        return 1.f;
    }
    return 0.f;
}

float midiRatio(float semitones) { return std::pow(2.f, semitones / 12.f); }

// Clips a control to [0, 1]. NaN gives `otherwise`.
float unitRange(float x, float otherwise = 0.f) {
    if (std::isnan(x))
        return otherwise;
    return std::min(std::max(x, 0.f), 1.f);
}

// The longest grain. After a very long grainDur, the grains that play end
// after this time at most, and the cloud starts again.
constexpr double kMaxGrainSeconds = 10.0;
// The longest grain in samples at any sample rate. It keeps the sample
// counter in an int.
constexpr double kMaxGrainSamples = 1 << 30;

// The fade-out of a grain that would read across the record head of a live
// source (see LiveSource).
constexpr double kFadeSeconds = 0.005;

// The time constants of the level control, from the granular player of
// Clouds: there the grain count moves by 0.9 (rise) or 0.2 (fall) of the
// difference in each block of 1 ms, and the gain by 0.01 in each sample at
// 32 kHz.
constexpr double kCountRiseSeconds = 0.000434; // -1 ms / ln(0.1)
constexpr double kCountFallSeconds = 0.00448; // -1 ms / ln(0.8)
constexpr double kGainSeconds = 0.00311; // -1 / (32000 * ln(0.99))

// The shortest interval, as a part of the regular interval. It keeps the
// phase increment finite.
constexpr double kMinIntervalScale = 1e-4;

// Sets the window shape of a grain from the texture control.
void setWindow(Grain& g, float texture, double length) {
    g.triInc = 2. / length;
    g.tri = g.triInc;
    if (texture < 0.5f) {
        g.slope = 0.5f / (texture + 0.01f);
        g.smooth = 0.f;
    } else {
        g.slope = 0.f;
        g.smooth = (texture - 0.5f) * 2.f;
    }
}

// The window value from the Hann value and the triangle phase of the grain.
SUPERPOS_INLINE float windowGain(const Grain& g, float hann, double tri) {
    float x = static_cast<float>(tri < 1. ? tri : 2. - tri);
    x = std::max(0.f, x);
    if (g.slope > 0.f)
        return std::min(1.f, x * g.slope);
    return x + g.smooth * (hann - x);
}

// Folds `in` into [lo, hi] (reflects it at the limits), as sc_fold. NaN and
// infinity give lo + (hi - lo) / 2.
float fold(float in, float lo, float hi) {
    if (!std::isfinite(in))
        return lo + (hi - lo) * 0.5f;
    if (in >= lo && in <= hi)
        return in;
    const double range = static_cast<double>(hi) - lo;
    const double range2 = range + range;
    const double x = static_cast<double>(in) - lo;
    double c = x - range2 * std::floor(x / range2);
    if (c >= range)
        c = range2 - c;
    return static_cast<float>(c + lo);
}

// A finite control, or 0.
float finiteOrZero(float x) { return std::isfinite(x) ? x : 0.f; }

// Plays `count` samples of one grain into `left` and `right`, with linear
// interpolation. With Fading, the window is multiplied by the fade gain.
template <bool Fading> void playGrainWith(Grain& g, const BufferView& buf, float* left, float* right, int count) {
    const double loopMax = static_cast<double>(buf.frames);
    const int guardFrame = static_cast<int>(buf.frames) - 2;
    const uint32_t bufSamples = buf.frames * buf.channels;
    double phase = g.phase;
    double y1 = g.y1, y2 = g.y2;
    const double b1 = g.b1;
    float amp = g.amp;
    // The Hann window (texture 1) uses the Hann value as it is, as GrainBuf.
    const bool hannOnly = g.slope == 0.f && g.smooth >= 1.f;
    double tri = g.tri;
    float fade = g.fade;
    const int counter = g.counter;
    for (int j = 0; j < count; ++j) {
        phase = loopPosition(phase, loopMax);
        const int32_t iphase = static_cast<int32_t>(phase);
        const float* table1 = buf.data + iphase;
        const float* table2 = table1 + 1;
        if (iphase > guardFrame)
            table2 -= bufSamples;
        const float frac = static_cast<float>(phase - static_cast<double>(iphase));
        const float b = table1[0];
        const float c = table2[0];
        float gain = hannOnly ? amp : windowGain(g, amp, tri);
        if (Fading) {
            // The fade is in the last fadeFrom samples of the grain.
            if (counter - j <= g.fadeFrom)
                fade = std::max(0.f, fade - g.fadeStep);
            gain *= fade;
        }
        const float value = gain * (b + frac * (c - b));
        left[j] += value * g.pan1;
        right[j] += value * g.pan2;
        tri += g.triInc;

        const double y0 = b1 * y1 - y2;
        y2 = y1;
        y1 = y0;
        amp = static_cast<float>(y1 * y1);
        phase += g.rate;
    }
    g.phase = phase;
    g.y1 = y1;
    g.y2 = y2;
    g.amp = amp;
    g.tri = tri;
    g.fade = fade;
    g.counter -= count;
}

// Plays `count` samples of one grain (see playGrainWith) and returns count.
int playGrain(Grain& g, const BufferView& buf, float* left, float* right, int count) {
    if (g.fading)
        playGrainWith<true>(g, buf, left, right, count);
    else
        playGrainWith<false>(g, buf, left, right, count);
    return count;
}


} // namespace

void Random::seed(uint32_t a, uint32_t b, uint32_t c) {
    // Each component needs a minimum value, or it stays at zero.
    s1 = a < 2u ? 1243598713u : a;
    s2 = b < 8u ? 3093459404u : b;
    s3 = c < 16u ? 1821928721u : c;
}

uint32_t Random::next() {
    s1 = ((s1 & 0xfffffffeu) << 12) ^ (((s1 << 13) ^ s1) >> 19);
    s2 = ((s2 & 0xfffffff8u) << 4) ^ (((s2 << 2) ^ s2) >> 25);
    s3 = ((s3 & 0xfffffff0u) << 17) ^ (((s3 << 3) ^ s3) >> 11);
    return s1 ^ s2 ^ s3;
}

bool usableBuffer(const BufferView& buf) { return buf.data && buf.channels == 1 && buf.frames > 0; }

void FileSource::begin(const BufferView& buf, double sampleDur, int) {
    mFrames = static_cast<float>(buf.frames);
    mFramesRecip = buf.frames > 0 ? 1.f / mFrames : 0.f;
    const float rateScale = static_cast<float>(buf.sampleRate * sampleDur);
    double rate = static_cast<float>(mPointerRate * rateScale);
    // An infinite or NaN rate stops the pointer, as it would make the pointer
    // NaN for good.
    if (!std::isfinite(rate))
        rate = 0.;
    mRate = rate;
    mWork = mPointer;
}

double FileSource::wrapPointer(double pointer) const { return wrap(pointer, 0., static_cast<double>(mFrames)); }

double FileSource::place(const BufferView& buf, bool scattered, double scatterFrames, double, double) const {
    float pos = mPos;
    // As `(Phasor + TRand.ar(0, scatter * SampleRate.ir)) / frames` and
    // `.wrap(0, 1)` in the SynthDef that the controls come from.
    if (scattered)
        pos = static_cast<float>(wrap((mGrainPointer + scatterFrames) / buf.frames, 0., 1.));
    // The product is in float, as GrainBuf computes it.
    return pos * buf.frames;
}

void LiveSource::setInput(const float* in, int inStep, float* data, float freeze, float position) {
    mIn = in;
    mInStep = inStep;
    mData = data;
    // NaN does not freeze.
    mFrozen = freeze >= 0.5f;
    mPosition = unitRange(position);
}

void LiveSource::begin(const BufferView& buf, double, int n) {
    mTick = -1;
    const uint32_t frames = usableBuffer(buf) ? buf.frames : 0;
    mFrames = frames;
    mLimitsChanged = mFrozen != mWasFrozen || frames != mLastFrames || mData != mLastData;
    if (frames == 0) {
        mHeadStart = mHeadEnd = 0;
        return;
    }
    // The buffer can be smaller than before.
    mHeadStart = mHead % frames;
    mHeadEnd = mHeadStart;
    if (mFrozen || !mData || !mIn)
        return;
    // The whole block is written before the grains play. A grain that starts
    // at sample i reads only frames that were written before sample i (see
    // place()), so the frames that come later in this block do not matter.
    uint32_t h = mHeadStart;
    for (int i = 0; i < n; ++i) {
        mData[h] = mIn[i * mInStep];
        if (++h == frames)
            h = 0;
    }
    mHeadEnd = h;
}

double LiveSource::place(const BufferView& buf, bool scattered, double scatterFrames, double rate,
                         double length) const {
    const double frames = buf.frames;
    const double headSpeed = mFrozen ? 0.0 : 1.0;
    // The head at this sample. Frame head - 1 is the newest input.
    const double head = mHeadStart + (mFrozen ? 0 : std::max(0, mTick));
    // The change of the distance over the whole grain.
    const double drift = (headSpeed - rate) * length;
    const double lo = kMinDistance - std::min(0.0, drift);
    const double hi = frames - kMinDistance - mBlockSize - std::max(0.0, drift);
    if (!(lo <= hi))
        return std::nan("");
    double distance = mPosition * frames;
    if (scattered)
        distance += scatterFrames;
    distance = std::min(std::max(distance, lo), hi);
    return wrap(head - distance, 0., frames);
}

double LiveSource::samplesBeforeCross(const BufferView& buf, double phase, double rate) const {
    const double frames = buf.frames;
    // The limits of place() for the distance behind the head.
    const double lo = kMinDistance;
    const double hi = frames - kMinDistance - mBlockSize;
    const double distance = wrap(static_cast<double>(mHeadStart) - phase, 0., frames);
    if (!(distance >= lo && distance <= hi))
        return 0.;
    // The distance changes by this value in each sample.
    const double speed = (mFrozen ? 0.0 : 1.0) - rate;
    if (speed > 0.)
        return (hi - distance) / speed;
    if (speed < 0.)
        return (lo - distance) / speed;
    return INFINITY;
}

template <class Source> void Engine<Source>::init(Grain* pool, int maxGrains, double sampleRate, int blockSize) {
    mPool = pool;
    mMaxGrains = std::max(0, maxGrains);
    mDropped = 0;
    mFaded = 0;
    mNumActive = 0;
    mSampleRate = sampleRate;
    mSampleDur = 1. / sampleRate;
    mMaxGrainSamples = std::min(kMaxGrainSamples, std::max(4., kMaxGrainSeconds * sampleRate));
    mSlopeFactor = 1. / std::max(1, blockSize);
    mTrigPhase = 1.0;
    mTrigInc = 0.0;
    mTrigIncSet = false;
    mPrevTrig = 0.f;
    mTrigScale = 1.0;
    mSource.reset(std::max(1, blockSize));
    mStarted = 0;
    mGrainCount = 0.0;
    mGain = 1.0;
    mGainCoef = 1.0 - std::exp(-mSampleDur / kGainSeconds);
}

template <class Source> void Engine<Source>::applyLevel(float* left, float* right, int n, bool keepState) {
    // The mean number of grains that played in each sample of this block
    // (a grain that ends in the block counts for the samples that it
    // played), smoothed with the coefficient for the length of the block.
    const double target = static_cast<double>(mPlayed) / n;
    const double seconds = target > mGrainCount ? kCountRiseSeconds : kCountFallSeconds;
    const double count = mGrainCount + (1.0 - std::exp(-n * mSampleDur / seconds)) * (target - mGrainCount);
    const double gainTarget = 1.0 / std::sqrt(std::max(1.0, count));
    double gain = mGain;
    for (int i = 0; i < n; ++i) {
        gain += mGainCoef * (gainTarget - gain);
        left[i] = static_cast<float>(left[i] * gain);
        right[i] = static_cast<float>(right[i] * gain);
    }
    if (keepState) {
        mGrainCount = count;
        mGain = gain;
    }
}

template <class Source> double Engine<Source>::nextIntervalScale(float regularity) {
    if (regularity <= 0.f)
        return 1.0;
    // An exponential value with a mean of 1. 1 - u is in (0, 1], so the
    // logarithm is finite.
    const double e = -std::log(1.0 - mRandom.uniform());
    const double r = regularity;
    return 1.0 / std::max(kMinIntervalScale, (1.0 - r) + r * e);
}

template <class Source>
void Engine<Source>::prime(const BufferView& buf, const Controls& c, float* left, float* right) {
    advance(buf, c, left, right, 1, false);
}

template <class Source>
void Engine<Source>::process(const BufferView& buf, const Controls& c, float* left, float* right, int n) {
    advance(buf, c, left, right, n, true);
}

template <class Source>
void Engine<Source>::advance(const BufferView& buf, const Controls& c, float* left, float* right, int n,
                             bool keepState) {
    // A block without samples changes nothing (the level control divides by
    // n).
    if (n <= 0)
        return;
    mSource.begin(buf, mSampleDur, n);
    if (mSource.limitsChanged())
        limitGrains(buf);
    std::fill(left, left + n, 0.f);
    std::fill(right, right + n, 0.f);
    mPlayed = 0;
    playActive(buf, left, right, n);

    // The trigger frequency and its phase increment, in the float and double
    // steps of the SynthDef. The increment moves linearly over the block to a
    // new value.
    const float trigFreq = c.overlap / c.grainDur;
    const float freqMul = static_cast<float>(mSampleDur);
    double inc = static_cast<float>(trigFreq * freqMul);
    // A grainDur of 0, or an infinite or NaN control, gives no trigger. The
    // trigger state stays finite, so the cloud starts again when the controls
    // are valid.
    if (!std::isfinite(inc))
        inc = 0.;
    if (!mTrigIncSet) {
        mTrigInc = inc;
        mTrigIncSet = true;
    }
    double prevInc = mTrigInc;
    const double incSlope = (inc - prevInc) * mSlopeFactor;
    double trigPhase = mTrigPhase;
    const float regularity = unitRange(c.regularity);
    const float probability = unitRange(c.probability);
    // The constructor sample does not change the random state or the
    // interval (see prime()).
    const Random randomBefore = mRandom;
    const double scaleBefore = mTrigScale;

    for (int i = 0; i < n; ++i) {
        float trig = wrapTrigger(prevInc, trigPhase);
        if (trig > 0.f) {
            mTrigScale = nextIntervalScale(regularity);
            if (probability < 1.f && !(mRandom.uniform() < probability))
                trig = 0.f;
        }
        prevInc += incSlope;
        trigPhase += prevInc * mTrigScale;

        mSource.tick();
        if (trig > 0.f && mPrevTrig <= 0.f)
            startGrain(buf, c, left, right, i, n);
        mPrevTrig = trig;
    }

    if (!(c.normalize < 0.5f))
        applyLevel(left, right, n, keepState);

    mTrigInc = inc;
    mSource.end(keepState);
    if (keepState) {
        mTrigPhase = trigPhase;
    } else {
        mRandom = randomBefore;
        mTrigScale = scaleBefore;
    }
}

template <class Source> void Engine<Source>::playActive(const BufferView& buf, float* left, float* right, int n) {
    // A grain that has finished leaves the pool at the start of the next
    // block, and the last grain takes its place, as in GrainBuf. The order of
    // the grains sets the order of the sums.
    const bool canRead = usableBuffer(buf);
    for (int i = 0; i < mNumActive;) {
        Grain& g = mPool[i];
        if (!canRead)
            g.counter -= n;
        if (g.counter <= 0) {
            g = mPool[--mNumActive];
            continue;
        }
        if (canRead)
            mPlayed += playGrain(g, buf, left, right, std::min(g.counter, n));
        ++i;
    }
}

template <class Source> void Engine<Source>::limitGrains(const BufferView& buf) {
    if (!usableBuffer(buf))
        return;
    const double fadeSamples = std::max(1.0, std::round(kFadeSeconds * mSampleRate));
    for (int i = 0; i < mNumActive; ++i) {
        Grain& g = mPool[i];
        if (g.counter <= 0)
            continue;
        const double before = mSource.samplesBeforeCross(buf, g.phase, g.rate);
        if (!(before < g.counter))
            continue;
        // The grain plays on, and ends before it reaches the limit. The fade
        // is over its last samples, from the gain that it has now.
        const int remaining = static_cast<int>(std::floor(before));
        if (remaining < 1) {
            g.counter = 0;
            ++mFaded;
            continue;
        }
        const int length = static_cast<int>(std::min(fadeSamples, static_cast<double>(remaining)));
        if (!g.fading) {
            g.fading = true;
            g.fade = 1.f;
        }
        g.counter = remaining;
        g.fadeFrom = length;
        g.fadeStep = g.fade / static_cast<float>(length);
        ++mFaded;
    }
}

template <class Source>
void Engine<Source>::startGrain(const BufferView& buf, const Controls& c, float* left, float* right, int offset,
                                int n) {
    if (!usableBuffer(buf))
        return;
    if (mNumActive >= mMaxGrains) {
        ++mDropped;
        return;
    }

    // The random values of the grain, in the order detune, scatter, pan. A
    // control at 0 takes no random value, so that the output with all of
    // them at 0 is the same as without them.
    GrainStart start {};
    const float detuneStep = std::fabs(finiteOrZero(c.detuneStep));
    if (detuneStep > 0.f)
        start.detune = static_cast<float>(mRandom.uniform() * 2. - 1.) * detuneStep;
    const float scatter = finiteOrZero(c.scatter);
    // TRand.ar(0, scatter * SampleRate.ir): frames at the server rate.
    if (scatter != 0.f)
        start.scatter = mRandom.uniform() * scatter * mSampleRate;
    const float spread = unitRange(c.stereoSpread);
    float pan = finiteOrZero(c.pan);
    if (spread > 0.f)
        pan = static_cast<float>(pan - spread + mRandom.uniform() * 2. * spread);
    start.pan = fold(pan, -1.f, 1.f);

    const float bufRateScale = mSource.rateScale(buf, mSampleDur);
    float ratio = midiRatio(c.transpose);
    if (start.detune != 0.f)
        ratio *= midiRatio(start.detune);
    const double rate = ratio * bufRateScale;
    double counter = c.grainDur * mSampleRate;
    counter = std::max(4., counter);
    counter = std::min(mMaxGrainSamples, counter);
    const double phase = mSource.place(buf, scatter != 0.f, start.scatter, rate, counter);
    if (!std::isfinite(phase) || !std::isfinite(rate))
        return;

    Grain& g = mPool[mNumActive++];
    ++mStarted;
    mLastStart = start;
    // Equal-power pan over two channels, as GrainBuf with two channels.
    const float panAngle = std::min(std::max(start.pan * 0.5f + 0.5f, 0.f), 1.f) * kHalfPiF;
    g.pan1 = std::cos(panAngle);
    g.pan2 = std::sin(panAngle);
    g.rate = rate;
    g.phase = phase;
    g.counter = static_cast<int>(counter);
    // The Hann window as a sine recurrence: sin^2(w * (k + 1)) at sample k.
    const double w = kPi / counter;
    g.b1 = 2. * std::cos(w);
    g.y1 = std::sin(w);
    g.y2 = 0.;
    g.amp = static_cast<float>(g.y1 * g.y1);
    setWindow(g, unitRange(c.texture, 1.f), counter);
    g.fading = false;
    g.fade = 1.f;
    g.fadeStep = 0.f;
    g.fadeFrom = 0;

    mPlayed += playGrain(g, buf, left + offset, right + offset, std::min(g.counter, n - offset));
    // A grain that ends in its first block leaves the pool at once.
    if (g.counter <= 0)
        --mNumActive;
}

// The sources that the UGens use.
template class Engine<FileSource>;
template class Engine<LiveSource>;

} // namespace superpos
