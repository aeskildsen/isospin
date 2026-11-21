// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later AND GPL-3.0-or-later
//
// Parts of this file are adapted from SuperCollider 3.14.1
// (https://github.com/supercollider/supercollider): Random (RGen), Pink
// (PinkNoise, server/plugins/NoiseUGens.cpp), the trigger of Impulse
// (server/plugins/LFUGens.cpp), and the grain start, the Hann window
// recurrence and the grain length of GrainSin
// (server/plugins/GrainUGens.cpp).
//     Copyright (c) 2002 James McCartney. All rights reserved.
//     GrainUGens.cpp also says: "Created by Josh Parmenter on 2/4/05.
//     Copyright 2005 __MyCompanyName__. All rights reserved."
//     These files are free software under the GNU General Public License,
//     version 2 or (at your option) any later version. This file uses them
//     under version 3 or (at your option) any later version, which permits
//     their combination with the GNU Affero General Public License version 3
//     (section 13 of each licence).
#include "pulsar_core.h"

#include <algorithm>
#include <cmath>

namespace pulsar {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kPi = 3.141592653589793;

// Lag.kr(0.02) of the jitter noise, as in the SynthDef.
constexpr double kNoiseLag = 0.02;

// The rate factors of the LFOs, in Hz for e = 1. Index 2k is the duration LFO
// of train k, 2k + 1 its frequency LFO.
constexpr double kLfoRate[2 * kTrains] = { 6., 8., 6., 7., 6., 8. };

// The powers of the duration law of each train.
constexpr double kLawPower[kTrains] = { 1.1, 1.5, 1.1 };

// A value clipped into [lo, hi]; a NaN or infinite value gives otherwise.
float clipped(float x, float otherwise, float lo, float hi) {
    if (!std::isfinite(x))
        return otherwise;
    return std::fmin(std::fmax(x, lo), hi);
}

// An LFO value s (-1 to 1) mapped to [lo, hi] and scaled towards 1 by wobble:
// at wobble 1 the range is [lo, hi], at 0 the value is 1.
double range(double lo, double hi, double s, double wobble) {
    return 1. + wobble * (0.5 * (lo + hi) - 1. + 0.5 * (hi - lo) * s);
}

// The number of trailing zero bits of x, 0 to 15 (CTZ(x) & 15 in PinkNoise).
int lowRow(uint32_t x) {
    if (x == 0)
        return 0;
    int k = 0;
    while ((x & 1u) == 0) {
        x >>= 1;
        ++k;
    }
    return k & 15;
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

void Pink::init(Random& random) {
    mTotal = 0;
    for (uint32_t& die : mDice) {
        die = random.next() >> 13;
        mTotal += die;
    }
    // PinkNoise calculates one value in its constructor.
    next(random);
}

float Pink::next(Random& random) {
    // One random number chooses the row (by its trailing zero bits) and gives
    // its new value. The rows are 19-bit values.
    const uint32_t r = random.next();
    const int k = lowRow(r);
    const uint32_t value = r >> 13;
    mTotal += value - mDice[k];
    mDice[k] = value;
    const uint32_t white = random.next() >> 13;
    // PinkNoise writes the sum into the mantissa of a float in [2, 4) and
    // subtracts 3: the value is sum / 2^22 - 1.
    return static_cast<float>(static_cast<double>(mTotal + white) * (1.0 / 4194304.0) - 1.0);
}

void Core::init(double sampleRate) {
    *this = Core();
    mSampleRate = sampleRate > 0. && std::isfinite(sampleRate) ? sampleRate : 48000.;
}

void Core::seed(uint32_t a, uint32_t b, uint32_t c) {
    mRandom.seed(a, b, c);
    // The same values as Rand(0, 6.28) / 2pi, Rand(-3, 3) / 3, Rand(1, 1.5)
    // and Rand(1, 1.2) in the SynthDef.
    for (double& p : mPhaseLeft)
        p = mRandom.uniform();
    for (double& p : mPhaseRight)
        p = mRandom.uniform();
    mOffset[0] = 0.;
    for (int k = 1; k < kTrains; ++k)
        mOffset[k] = 2. * mRandom.uniform() - 1.;
    mTop0 = 1. + 0.5 * mRandom.uniform();
    mTop2 = 1. + 0.2 * mRandom.uniform();
    for (Pink& pink : mPink)
        pink.init(mRandom);
}

int Core::activeGrains() const {
    int sum = 0;
    for (const Train& t : mTrains)
        for (int c = 0; c < kChannels; ++c)
            sum += t.active[c];
    return sum;
}

void Core::startGrain(int train, int channel, double freq, double dur) {
    Train& t = mTrains[train];
    if (t.active[channel] >= kGrainsPerTrain) {
        ++mDropped;
        return;
    }
    // GrainSin: the length is max(4, dur * sampleRate) samples. The upper
    // limit (about 35 minutes at 48 kHz) only keeps the conversion to int
    // defined.
    double length = dur * mSampleRate;
    if (!(length >= 4.))
        length = 4.;
    length = std::fmin(length, 1.0e8);
    Grain& g = t.grains[channel][t.active[channel]++];
    g.remaining = static_cast<int>(length);
    // The Hann window sin^2(pi * (i + 1) / length) as a recurrence, as in
    // GrainSin.
    const double w = kPi / static_cast<double>(g.remaining);
    g.winB1 = 2. * std::cos(w);
    g.winY1 = std::sin(w);
    g.winY2 = 0.;
    // The sine sin(omega * i) as a recurrence, from phase 0.
    const double omega = kTwoPi * freq / mSampleRate;
    g.oscB1 = 2. * std::cos(omega);
    g.oscY1 = 0.;
    g.oscY2 = -std::sin(omega);
}

void Core::render(Grain& g, float* acc, int from, int to) {
    const int end = std::min(to, from + g.remaining);
    double wb = g.winB1, wy1 = g.winY1, wy2 = g.winY2;
    double ob = g.oscB1, oy1 = g.oscY1, oy2 = g.oscY2;
    for (int i = from; i < end; ++i) {
        acc[i] += static_cast<float>(wy1 * wy1 * oy1);
        const double w0 = wb * wy1 - wy2;
        wy2 = wy1;
        wy1 = w0;
        const double o0 = ob * oy1 - oy2;
        oy2 = oy1;
        oy1 = o0;
    }
    g.winY1 = wy1;
    g.winY2 = wy2;
    g.oscY1 = oy1;
    g.oscY2 = oy2;
    g.remaining -= end - from;
}

void Core::process(const Controls& in, float* left, float* right, int n) {
    if (n <= 0)
        return;
    const double sr = mSampleRate;

    // The controls, clipped into their ranges.
    if (std::isfinite(in.freq))
        mLastFreq = static_cast<float>(std::fmin(std::fmax(in.freq, 1.f), 0.5 * sr));
    const double freq = mLastFreq;
    const double energy = clipped(in.energy, 0.f, 0.f, 1.f);
    const double velocity = clipped(in.velocity, 0.5f, 0.f, 1.f);
    const double ratio[kTrains] = { 1., clipped(in.formant1, 2.f, 0.25f, 16.f), clipped(in.formant2, 6.f, 0.25f, 16.f) };
    const double overlap[kTrains] = { clipped(in.overlap0, 1.f, 0.f, 4.f), clipped(in.overlap1, 2.f, 0.f, 4.f),
                                      clipped(in.overlap2, 2.f, 0.f, 4.f) };
    const double wobble = clipped(in.wobble, 1.f, 0.f, 2.f);
    const double detune = clipped(in.detune, 3.f, 0.f, 20.f);
    const double jitter = clipped(in.jitter, 1.f, 0.f, 1.f);
    const double law = clipped(in.law, 0.f, 0.f, 1.f);
    const double width = clipped(in.width, 1.f, 0.f, 1.f);

    // Control rate: the noise, e and the gains of this block.
    const double lagB1 = std::exp(std::log(0.001) / (kNoiseLag * sr / n));
    for (int c = 0; c < kChannels; ++c) {
        const double raw = mPink[c].next(mRandom);
        mNoise[c] = mFirst ? raw : raw + lagB1 * (mNoise[c] - raw);
    }
    const double theta = width * 0.5 * kPi;
    const double noise[kChannels] = { mNoise[0], mNoise[0] * std::cos(theta) + mNoise[1] * std::sin(theta) };

    // velocity 0 to 1 gives the level 0.003 to 0.3 on a log scale.
    const double lev = 0.003 * std::pow(100., velocity);
    const double u = (std::fmin(std::fmax(freq, 50.), 1500.) - 50.) / 1450.;
    const double gainPower[kTrains] = { 0.7, 1.1 + 0.4 * u, 1.5 + u };

    double eStart[kChannels], eEnd[kChannels];
    double gStart[kChannels][kTrains], gEnd[kChannels][kTrains];
    double phase0[kChannels][2 * kTrains], lfoInc[kChannels][2 * kTrains];
    for (int c = 0; c < kChannels; ++c) {
        // e = energy * PinkNoise.range(0.1, 1) at jitter 1; jitter 0 gives no
        // noise.
        eEnd[c] = energy * std::fmax(0., 1. - jitter * (0.45 - 0.45 * noise[c]));
        eStart[c] = mFirst ? eEnd[c] : mEnergy[c];
        for (int k = 0; k < kTrains; ++k) {
            gEnd[c][k] = std::pow(lev * eEnd[c], gainPower[k]);
            gStart[c][k] = mFirst ? gEnd[c][k] : mGain[c][k];
        }
        for (int j = 0; j < 2 * kTrains; ++j) {
            phase0[c][j] = c == 0 ? mPhaseLeft[j] : mPhaseLeft[j] + width * (mPhaseRight[j] - mPhaseLeft[j]);
            lfoInc[c][j] = eEnd[c] * kLfoRate[j] / sr;
        }
    }

    double trigInc[kTrains];
    for (int k = 0; k < kTrains; ++k)
        trigInc[k] = std::fmax(1., freq + detune * mOffset[k]) / sr;
    const double freqLo[kTrains] = { 0.9, 0.95, 0.95 };
    const double freqHi[kTrains] = { mTop0, 1.05, mTop2 };
    const double invN = 1. / n;

    float* outs[kChannels] = { left, right };
    for (int start = 0; start < n; start += kChunk) {
        const int m = std::min(kChunk, n - start);
        float acc[kChannels][kTrains][kChunk] = {};

        // The grains that play from earlier blocks.
        for (int k = 0; k < kTrains; ++k) {
            Train& t = mTrains[k];
            for (int c = 0; c < kChannels; ++c) {
                for (int g = 0; g < t.active[c];) {
                    render(t.grains[c][g], acc[c][k], 0, m);
                    if (t.grains[c][g].remaining <= 0)
                        t.grains[c][g] = t.grains[c][--t.active[c]];
                    else
                        ++g;
                }
            }
        }

        // New grains: Impulse gives a trigger when its phase reaches 1.
        for (int k = 0; k < kTrains; ++k) {
            Train& t = mTrains[k];
            for (int i = 0; i < m; ++i) {
                const bool trigger = t.phase >= 1.;
                if (trigger)
                    t.phase -= 1.;
                t.phase += trigInc[k];
                if (!trigger)
                    continue;
                const int at = start + i;
                for (int c = 0; c < kChannels; ++c) {
                    const double e = eStart[c] + (eEnd[c] - eStart[c]) * at * invN;
                    const double sDur = std::sin(kTwoPi * (phase0[c][2 * k] + mLfo[c][2 * k] + lfoInc[c][2 * k] * at));
                    const double sFreq
                        = std::sin(kTwoPi * (phase0[c][2 * k + 1] + mLfo[c][2 * k + 1] + lfoInc[c][2 * k + 1] * at));
                    const double ep = std::pow(e, kLawPower[k]);
                    const double shape = 1.05 - ep + law * (2. * ep - 1.);
                    const double dur = overlap[k] * shape * range(0.87, 1.15, sDur, wobble) / freq;
                    const double grainFreq = freq * ratio[k] * range(freqLo[k], freqHi[k], sFreq, wobble);
                    const int before = t.active[c];
                    startGrain(k, c, grainFreq, dur);
                    if (t.active[c] > before) {
                        Grain& g = t.grains[c][before];
                        render(g, acc[c][k], i, m);
                        if (g.remaining <= 0)
                            g = t.grains[c][--t.active[c]];
                    }
                }
            }
        }

        // Mix with the gains, interpolated over the block, then LeakDC.
        for (int c = 0; c < kChannels; ++c) {
            float* out = outs[c] + start;
            double x1 = mDcX[c], y1 = mDcY[c];
            for (int i = 0; i < m; ++i) {
                const double f = (start + i) * invN;
                double x = 0.;
                for (int k = 0; k < kTrains; ++k)
                    x += (gStart[c][k] + (gEnd[c][k] - gStart[c][k]) * f) * acc[c][k][i];
                const double y = x - x1 + 0.995 * y1;
                x1 = x;
                y1 = y;
                out[i] = static_cast<float>(y);
            }
            mDcX[c] = x1;
            mDcY[c] = y1;
        }
    }

    // The state for the next block.
    for (int c = 0; c < kChannels; ++c) {
        mEnergy[c] = eEnd[c];
        for (int k = 0; k < kTrains; ++k)
            mGain[c][k] = gEnd[c][k];
        for (int j = 0; j < 2 * kTrains; ++j) {
            const double p = mLfo[c][j] + lfoInc[c][j] * n;
            mLfo[c][j] = p - std::floor(p);
        }
    }
    mFirst = false;
}

} // namespace pulsar
