// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests of the Pulsar DSP core. scripts/check-native.py builds them with
// AddressSanitizer and UndefinedBehaviorSanitizer. Exit code 0 is a pass.
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "pulsar_core.h"

namespace {

constexpr double kSampleRate = 48000.;
constexpr int kBlock = 64;
constexpr double kPi = 3.141592653589793;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Stereo {
    std::vector<float> left, right;
};

// Runs the core in blocks of blockSize for `total` samples.
Stereo render(const pulsar::Controls& c, int total, int blockSize = kBlock, pulsar::Core* coreOut = nullptr,
              uint32_t seed = 1) {
    pulsar::Core local;
    pulsar::Core& core = coreOut ? *coreOut : local;
    core.init(kSampleRate);
    core.seed(seed, seed * 7u + 11u, seed * 13u + 17u);
    Stereo s;
    std::vector<float> l(blockSize), r(blockSize);
    while (static_cast<int>(s.left.size()) < total) {
        core.process(c, l.data(), r.data(), blockSize);
        s.left.insert(s.left.end(), l.begin(), l.end());
        s.right.insert(s.right.end(), r.begin(), r.end());
    }
    return s;
}

// The controls without random parts: no wobble, jitter or detune, the
// channels the same.
pulsar::Controls plain() {
    pulsar::Controls c;
    c.freq = 220.f;
    c.energy = 0.5f;
    c.velocity = 0.6f;
    c.wobble = 0.f;
    c.jitter = 0.f;
    c.detune = 0.f;
    c.width = 0.f;
    return c;
}

// The output of plain() controls, calculated directly: the GrainSin grains
// with std::sin, the trains mixed with their gains, then LeakDC.
std::vector<double> expected(const pulsar::Controls& c, int total) {
    const double freq = c.freq, e = c.energy;
    const double lev = 0.003 * std::pow(100., c.velocity);
    const double u = (freq - 50.) / 1450.;
    const double gainPower[3] = { 0.7, 1.1 + 0.4 * u, 1.5 + u };
    const double lawPower[3] = { 1.1, 1.5, 1.1 };
    const double ratio[3] = { 1., c.formant1, c.formant2 };
    const double overlap[3] = { c.overlap0, c.overlap1, c.overlap2 };
    std::vector<double> mix(total, 0.);
    for (int k = 0; k < 3; ++k) {
        const double gain = std::pow(lev * e, gainPower[k]);
        const double dur = overlap[k] * (1.05 - std::pow(e, lawPower[k])) / freq;
        const int length = std::max(4, static_cast<int>(dur * kSampleRate));
        const double f = freq * ratio[k];
        // Impulse: a trigger when the phase reaches 1; the phase starts at 1.
        double phase = 1.;
        for (int t = 0; t < total; ++t) {
            const bool trigger = phase >= 1.;
            if (trigger)
                phase -= 1.;
            phase += freq / kSampleRate;
            if (!trigger)
                continue;
            for (int i = 0; i < length && t + i < total; ++i) {
                const double w = std::sin(kPi * (i + 1) / length);
                mix[t + i] += gain * w * w * std::sin(2. * kPi * f * i / kSampleRate);
            }
        }
    }
    std::vector<double> out(total);
    double x1 = 0., y1 = 0.;
    for (int t = 0; t < total; ++t) {
        const double y = mix[t] - x1 + 0.995 * y1;
        x1 = mix[t];
        y1 = y;
        out[t] = y;
    }
    return out;
}

void testGrains() {
    const int total = 48000;
    for (float overlap : { 0.5f, 1.f, 2.f, 4.f }) {
        pulsar::Controls c = plain();
        c.overlap0 = overlap;
        c.overlap1 = overlap;
        c.overlap2 = overlap;
        const Stereo s = render(c, total);
        const std::vector<double> want = expected(c, total);
        double worst = 0., peak = 0.;
        for (int t = 0; t < total; ++t) {
            worst = std::fmax(worst, std::fabs(s.left[t] - want[t]));
            peak = std::fmax(peak, std::fabs(want[t]));
        }
        std::printf("grains: overlap %.1f: peak %.4f, largest difference %.2e\n", overlap, peak, worst);
        check(peak > 0.01, "grains: the output is not silent");
        check(worst < 1e-5 * peak + 1e-7, "grains: the output is the direct GrainSin calculation");
    }
}

void testWidth() {
    pulsar::Controls c;
    c.freq = 330.f;
    c.energy = 0.8f;
    c.width = 0.f;
    Stereo s = render(c, 24000);
    bool same = true;
    for (size_t t = 0; t < s.left.size(); ++t)
        same = same && s.left[t] == s.right[t];
    check(same, "width 0: the two channels are the same");
    c.width = 1.f;
    s = render(c, 24000);
    double diff = 0.;
    for (size_t t = 0; t < s.left.size(); ++t)
        diff = std::fmax(diff, std::fabs(s.left[t] - s.right[t]));
    std::printf("width 1: largest difference between the channels %.4f\n", diff);
    check(diff > 1e-3, "width 1: the two channels are different");
}

void testSilence() {
    pulsar::Controls c;
    c.energy = 0.f;
    const Stereo s = render(c, 12000);
    bool silent = true;
    for (size_t t = 0; t < s.left.size(); ++t)
        silent = silent && s.left[t] == 0.f && s.right[t] == 0.f;
    check(silent, "energy 0: the output is silent");
}

void testPool() {
    // The values that make the most grains at the same time.
    long dropped = 0;
    for (float freq : { 100.f, 150.f, 400.f, 1500.f, 5000.f }) {
        pulsar::Controls c;
        c.freq = freq;
        c.energy = 1.f;
        c.overlap0 = c.overlap1 = c.overlap2 = 4.f;
        c.wobble = 2.f;
        c.detune = 20.f;
        c.law = 1.f;
        c.jitter = 1.f;
        for (uint32_t seed = 1; seed <= 8; ++seed) {
            pulsar::Core core;
            render(c, 48000, kBlock, &core, seed);
            dropped += core.droppedGrains();
        }
    }
    std::printf("pool: %ld grains dropped at the largest values from 100 Hz\n", dropped);
    check(dropped == 0, "pool: no grain is dropped from 100 Hz");

    dropped = 0;
    for (float freq : { 20.f, 30.f, 50.f }) {
        pulsar::Controls c;
        c.freq = freq;
        c.energy = 1.f;
        c.overlap0 = c.overlap1 = c.overlap2 = 4.f;
        for (uint32_t seed = 1; seed <= 8; ++seed) {
            pulsar::Core core;
            render(c, 96000, kBlock, &core, seed);
            dropped += core.droppedGrains();
        }
    }
    std::printf("pool: %ld grains dropped at the default wobble, detune and law from 20 Hz\n", dropped);
    check(dropped == 0, "pool: no grain is dropped at default wobble, detune and law from 20 Hz");
}

void testBadValues() {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float values[] = { nan, inf, -inf, 0.f, -1.f, 1e30f, -1e30f, 1e-30f };
    bool finite = true;
    for (int field = 0; field < 13; ++field) {
        for (float v : values) {
            pulsar::Controls c;
            float* f = &c.freq + field;
            *f = v;
            for (int blockSize : { 1, 64, 200 }) {
                const Stereo s = render(c, 2000, blockSize);
                for (size_t t = 0; t < s.left.size(); ++t)
                    finite = finite && std::isfinite(s.left[t]) && std::isfinite(s.right[t]);
            }
        }
    }
    check(finite, "bad values: the output stays finite");

    // A valid freq after NaN: the sound continues.
    pulsar::Core core;
    core.init(kSampleRate);
    core.seed(5, 6, 7);
    std::vector<float> l(kBlock), r(kBlock);
    pulsar::Controls c;
    c.freq = nan;
    for (int b = 0; b < 100; ++b)
        core.process(c, l.data(), r.data(), kBlock);
    c.freq = 200.f;
    double peak = 0.;
    for (int b = 0; b < 100; ++b) {
        core.process(c, l.data(), r.data(), kBlock);
        for (float x : l)
            peak = std::fmax(peak, std::fabs(x));
    }
    check(peak > 0.01, "bad values: a valid freq after NaN plays");
}

void testBlockSizes() {
    // The grains do not depend on the block size; with no random parts and a
    // constant energy, all block sizes give the same output.
    const pulsar::Controls c = plain();
    const Stereo a = render(c, 9600, 64);
    for (int blockSize : { 1, 50, 128, 1000 }) {
        const Stereo b = render(c, 9600, blockSize);
        double worst = 0.;
        for (int t = 0; t < 9600; ++t)
            worst = std::fmax(worst, std::fabs(a.left[t] - b.left[t]));
        std::printf("block size %d: largest difference from block size 64 %.2e\n", blockSize, worst);
        check(worst < 1e-6, "block sizes: the same output");
    }
}

} // namespace

int main() {
    testGrains();
    testWidth();
    testSilence();
    testPool();
    testBadValues();
    testBlockSizes();
    if (failures) {
        std::printf("pulsar_core_test: %d failures\n", failures);
        return 1;
    }
    std::printf("pulsar_core_test: OK\n");
    return 0;
}
