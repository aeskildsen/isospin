// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "superpos_core.h"

#include <algorithm>
#include <cmath>

namespace superpos {

namespace {

// constexpr: SuperSonic's web build does not run the dynamic initialisation
// of namespace-scope variables, so std::acos(-1.) here would stay 0.
constexpr double kPi = 3.141592653589793238462643383279502884;

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

// Wraps a read position into [0, hi), as sc_gloop of GrainBuf.
double loopPosition(double in, double hi) {
    if (in >= hi) {
        in -= hi;
        if (in < hi)
            return in;
    } else if (in < 0.) {
        in += hi;
        if (in >= 0.)
            return in;
    } else {
        return in;
    }
    return in - hi * std::floor(in / hi);
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

// Plays `count` samples of one grain into `out`, with linear interpolation.
void playGrain(Grain& g, const BufferView& buf, float* out, int count) {
    const double loopMax = static_cast<double>(buf.frames);
    const int guardFrame = static_cast<int>(buf.frames) - 2;
    const uint32_t bufSamples = buf.frames * buf.channels;
    double phase = g.phase;
    double y1 = g.y1, y2 = g.y2;
    const double b1 = g.b1;
    float amp = g.amp;
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
        out[j] += amp * (b + frac * (c - b));

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
    g.counter -= count;
}

} // namespace

void Core::init(Grain* pool, int maxGrains, double sampleRate, int blockSize) {
    mPool = pool;
    mMaxGrains = std::max(0, maxGrains);
    mNumActive = 0;
    mSampleRate = sampleRate;
    mSampleDur = 1. / sampleRate;
    mSlopeFactor = 1. / std::max(1, blockSize);
    mTrigPhase = 1.0;
    mTrigInc = 0.0;
    mTrigIncSet = false;
    mPrevTrig = 0.f;
    mPointer = 0.0;
}

bool Core::usable(const BufferView& buf) { return buf.data && buf.channels == 1 && buf.frames > 0; }

void Core::prime(const BufferView& buf, const Controls& c, float* out) { advance(buf, c, out, 1, false); }

void Core::process(const BufferView& buf, const Controls& c, float* out, int n) { advance(buf, c, out, n, true); }

void Core::advance(const BufferView& buf, const Controls& c, float* out, int n, bool keepState) {
    std::fill(out, out + n, 0.f);
    playActive(buf, out, n);

    // The trigger frequency and its phase increment, in the float and double
    // steps of the SynthDef. The increment moves linearly over the block to a
    // new value.
    const float trigFreq = c.overlap * (1.f / c.grainDur);
    const float freqMul = static_cast<float>(mSampleDur);
    const double inc = static_cast<float>(trigFreq * freqMul);
    if (!mTrigIncSet) {
        mTrigInc = inc;
        mTrigIncSet = true;
    }
    double prevInc = mTrigInc;
    const double incSlope = (inc - prevInc) * mSlopeFactor;
    double trigPhase = mTrigPhase;

    // The read pointer moves in frames from 0 to the end of the buffer. The
    // grain position is the pointer as a part of the buffer length.
    const float frames = static_cast<float>(buf.frames);
    const float rateScale = static_cast<float>(buf.sampleRate * mSampleDur);
    const double pointerRate = static_cast<float>(c.pointerRate * rateScale);
    double pointer = mPointer;

    for (int i = 0; i < n; ++i) {
        const float trig = wrapTrigger(prevInc, trigPhase);
        prevInc += incSlope;
        trigPhase += prevInc;

        pointer = wrap(pointer, 0., static_cast<double>(frames));
        const float pos = static_cast<float>(pointer) / frames;
        pointer += pointerRate;

        if (trig > 0.f && mPrevTrig <= 0.f)
            startGrain(buf, c, pos, out, i, n);
        mPrevTrig = trig;
    }

    mTrigInc = inc;
    if (keepState) {
        mTrigPhase = trigPhase;
        mPointer = pointer;
    }
}

void Core::playActive(const BufferView& buf, float* out, int n) {
    // A grain that has finished leaves the pool at the start of the next
    // block, and the last grain takes its place, as in GrainBuf. The order of
    // the grains sets the order of the sums.
    const bool canRead = usable(buf);
    for (int i = 0; i < mNumActive;) {
        Grain& g = mPool[i];
        if (!canRead)
            g.counter -= n;
        if (g.counter <= 0) {
            g = mPool[--mNumActive];
            continue;
        }
        if (canRead)
            playGrain(g, buf, out, std::min(g.counter, n));
        ++i;
    }
}

void Core::startGrain(const BufferView& buf, const Controls& c, float pos, float* out, int offset, int n) {
    if (mNumActive >= mMaxGrains || !usable(buf))
        return;

    const float bufRateScale = static_cast<float>(static_cast<float>(buf.sampleRate) * mSampleDur);
    const double rate = midiRatio(c.transpose) * bufRateScale;
    const double phase = pos * buf.frames;
    if (!std::isfinite(phase))
        return;
    double counter = c.grainDur * mSampleRate;
    counter = std::max(4., counter);

    Grain& g = mPool[mNumActive++];
    g.rate = rate;
    g.phase = phase;
    g.counter = static_cast<int>(counter);
    // The Hann window as a sine recurrence: sin^2(w * (k + 1)) at sample k.
    const double w = kPi / counter;
    g.b1 = 2. * std::cos(w);
    g.y1 = std::sin(w);
    g.y2 = 0.;
    g.amp = static_cast<float>(g.y1 * g.y1);

    playGrain(g, buf, out + offset, std::min(g.counter, n - offset));
    // A grain that ends in its first block leaves the pool at once.
    if (g.counter <= 0)
        --mNumActive;
}

} // namespace superpos
