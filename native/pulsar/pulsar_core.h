// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parts of pulsar_core.cpp are adapted from SuperCollider. See the notice at
// the top of that file.
//
// The DSP core of the Pulsar UGen. It uses no SuperCollider headers, so that a
// test program can use it without a server.
//
// Pulsar synthesis after the pulsar SynthDef of Alik Rustamoff's "Musical
// sound design in SuperCollider" series (episode 1), which uses three GrainSin
// UGens. reference/pulsar.dsp is the same design in Faust; it is the
// specification of the controls, but it is not in the release, because Faust
// calculates all grain voices in each sample, also the silent ones.
//
// Three trains of sine grains:
//   train 0  the fundamental, grain frequency freq
//   train 1  the first formant, grain frequency freq * formant1
//   train 2  the second formant, grain frequency freq * formant2
// Each train starts a grain at each period of its trigger frequency, as
// Impulse.ar. Trains 1 and 2 trigger at freq plus a random offset in
// +-detune Hz. A grain is a sine that starts at phase 0, under the Hann window
// of GrainSin: the same recurrence, the same length in samples
// (max(4, dur * sampleRate)). The grain holds its frequency and duration from
// the sample where it starts.
//
// energy (0 to 1, usually an envelope) controls the sound in three ways:
//   level     each train has the gain (lev * e)^p, where lev comes from
//             velocity, p is 0.7 for train 0, 1.1 to 1.5 for train 1 and
//             1.5 to 2.5 for train 2 (over freq 50 to 1500 Hz);
//   duration  the grain duration is overlap * law(e) * lfo / freq, where
//             law(e) goes from 1.05 - e^q (law 0) to 0.05 + e^q (law 1),
//             with q 1.1, 1.5 and 1.1 for the three trains;
//   wobble    sine LFOs with a rate of e * 6 Hz (duration) and e * 8, 7, 8 Hz
//             (frequency) move the grain durations and frequencies.
// e is energy times a jitter factor from pink noise, separately in each
// channel. As in the SynthDef, e, the noise and the gains are control-rate
// values: they change once in each block, and the gains are interpolated
// over the block. The LFOs are calculated only where a grain starts.
//
// The two channels share the triggers. Each channel has its own noise and LFO
// phases. width moves the right channel from the values of the left channel
// (0, the channels are the same) to its own values (1).
//
// The random values of a note (LFO phases, trigger offsets, the upper ends of
// two LFO ranges, the noise) come from the generator that seed() gets. The
// UGen seeds it from the random generator of the synth, as Rand.
//
// Each train has a pool of kGrainsPerTrain grains in each channel. With freq
// at 100 Hz or more, the pool does not get full at any control values. With
// wobble, detune and law at their defaults, this is true from freq 20 Hz.
// When a pool is full, a new grain does not start.
#pragma once

#include <cstdint>

namespace pulsar {

constexpr int kTrains = 3;
constexpr int kChannels = 2;
constexpr int kGrainsPerTrain = 16;
// The core mixes the trains in parts of at most this many samples.
constexpr int kChunk = 64;

// The controls. The core clips each value into its range; a NaN or infinite
// value gets the default (freq keeps its last valid value).
struct Controls {
    float freq = 440.f; // Hz, 1 to sampleRate / 2
    float energy = 1.f; // 0 to 1
    float velocity = 0.5f; // 0 to 1
    float formant1 = 2.f; // 0.25 to 16
    float formant2 = 6.f; // 0.25 to 16
    float overlap0 = 1.f; // 0 to 4
    float overlap1 = 2.f; // 0 to 4
    float overlap2 = 2.f; // 0 to 4
    float wobble = 1.f; // 0 to 2
    float detune = 3.f; // Hz, 0 to 20
    float jitter = 1.f; // 0 to 1
    float law = 0.f; // 0 to 1
    float width = 1.f; // 0 to 1
};

// The random generator of SuperCollider (RGen, Tausworthe 88).
class Random {
public:
    void seed(uint32_t a, uint32_t b, uint32_t c);
    uint32_t next();
    // A value in [0, 1).
    double uniform() { return next() * (1.0 / 4294967296.0); }

private:
    uint32_t s1 = 1243598713u, s2 = 3093459404u, s3 = 1821928721u;
};

// Pink noise as PinkNoise of SuperCollider (Voss-McCartney, 16 rows), one
// value for each call. The output is about -1 to 1.
class Pink {
public:
    void init(Random& random);
    float next(Random& random);

private:
    uint32_t mDice[16] = {};
    uint32_t mTotal = 0;
};

// One grain: two recurrences, the Hann window and the sine.
struct Grain {
    double winB1, winY1, winY2; // window: amp = winY1^2
    double oscB1, oscY1, oscY2; // sine: the output is oscY1
    int remaining; // samples to play
};

class Core {
public:
    // Call once before process(). sampleRate in Hz.
    void init(double sampleRate);
    // Draws the random values of the note. Call after init() and before the
    // first process().
    void seed(uint32_t a, uint32_t b, uint32_t c);
    // Writes n samples (n >= 1) to left and right. One call is one block: the
    // control-rate values change once in each call.
    void process(const Controls& controls, float* left, float* right, int n);

    // For the tests: the grains that play now, and the grains that did not
    // start because a pool was full.
    int activeGrains() const;
    long droppedGrains() const { return mDropped; }

private:
    struct Train {
        double phase = 1.0; // Impulse: 1 gives a trigger in the first sample
        Grain grains[kChannels][kGrainsPerTrain];
        int active[kChannels] = {};
    };

    void startGrain(int train, int channel, double freq, double dur);
    static void render(Grain& g, float* acc, int from, int to);

    double mSampleRate = 48000.;
    Random mRandom;
    Train mTrains[kTrains];
    float mLastFreq = 440.f;
    bool mFirst = true;

    // The random values of the note.
    double mPhaseLeft[2 * kTrains] = {}; // LFO start phases in cycles
    double mPhaseRight[2 * kTrains] = {};
    double mOffset[kTrains] = {}; // trigger offsets in [-1, 1] (train 0: 0)
    double mTop0 = 1., mTop2 = 1.;

    // Control-rate state of each channel.
    Pink mPink[kChannels];
    double mNoise[kChannels] = {}; // the lagged noise
    double mEnergy[kChannels] = {}; // e at the end of the last block
    double mGain[kChannels][kTrains] = {}; // gains at the end of the last block
    double mLfo[kChannels][2 * kTrains] = {}; // LFO phase accumulators in cycles
    double mDcX[kChannels] = {}, mDcY[kChannels] = {}; // LeakDC
    long mDropped = 0;
};

} // namespace pulsar
