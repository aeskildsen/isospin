// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests of the SuperPos DSP core. scripts/check-native.py builds them with
// AddressSanitizer and UndefinedBehaviorSanitizer. Exit code 0 is a pass.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "superpos_core.h"

namespace {

constexpr double kSampleRate = 48000.;
constexpr int kBlock = 64;
constexpr int kMaxGrains = 64;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The controls with the pan hard left. The left channel then has a gain of
// exactly 1 (cos 0), so it is the mono sum of the grains.
superpos::Controls hardLeft(superpos::Controls c) {
    c.pan = -1.f;
    return c;
}

// Runs the core as the UGen does: one primed sample, then whole blocks.
// Returns `total` samples of the left channel, from the first block on (the
// primed sample is not in it); `rightOut` gets the right channel. The read
// pointer of the file source moves at `pointerRate`.
std::vector<float> render(const superpos::BufferView& buf, const superpos::Controls& c, int total,
                          int* maxActive = nullptr, std::vector<float>* rightOut = nullptr,
                          float pointerRate = 1.f) {
    std::vector<superpos::Grain> pool(kMaxGrains);
    superpos::Core core;
    core.init(pool.data(), kMaxGrains, kSampleRate, kBlock);
    core.source().setPointerRate(pointerRate);
    float primedLeft = 0.f, primedRight = 0.f;
    core.prime(buf, c, &primedLeft, &primedRight);
    int most = core.activeGrains();
    // The primed sample is the output of the constructor; the first block
    // starts again at index 0 of the server's buffer.
    std::vector<float> block(kBlock), blockRight(kBlock);
    std::vector<float> result, right;
    result.reserve(total);
    while (static_cast<int>(result.size()) < total) {
        core.process(buf, c, block.data(), blockRight.data(), kBlock);
        if (core.activeGrains() > most)
            most = core.activeGrains();
        result.insert(result.end(), block.begin(), block.end());
        right.insert(right.end(), blockRight.begin(), blockRight.end());
    }
    result.resize(total);
    right.resize(total);
    if (rightOut)
        *rightOut = right;
    if (maxActive)
        *maxActive = most;
    return result;
}

void testNoBuffer() {
    const superpos::BufferView none { nullptr, 0, 1, kSampleRate };
    const superpos::Controls c { 0.01f, 4.f, 0.f };
    int active = 0;
    const std::vector<float> out = render(none, c, 4800, &active);
    bool silent = true;
    for (float v : out)
        silent = silent && v == 0.f;
    check(silent, "no buffer: the output is silent");
    check(active == 0, "no buffer: no grain starts");
}

void testStereoBuffer() {
    std::vector<float> data(2 * 1000, 1.f);
    const superpos::BufferView stereo { data.data(), 1000, 2, kSampleRate };
    const superpos::Controls c { 0.01f, 4.f, 0.f };
    int active = 0;
    const std::vector<float> out = render(stereo, c, 4800, &active);
    bool silent = true;
    for (float v : out)
        silent = silent && v == 0.f;
    check(silent, "two-channel buffer: the output is silent");
    check(active == 0, "two-channel buffer: no grain starts");
}

// With a buffer of ones, one grain gives its window. The first grain starts
// in the primed sample, so the first block holds window samples 1, 2, ...
void testWindowShape() {
    std::vector<float> data(1000, 1.f);
    const superpos::BufferView ones { data.data(), 1000, 1, kSampleRate };
    // 480 samples a grain, a grain each 960 samples: no overlap.
    const superpos::Controls c = hardLeft({ 0.01f, 0.5f, 0.f });
    const std::vector<float> out = render(ones, c, 900);
    const int n = 480;
    double worst = 0.;
    for (int k = 1; k < n; ++k) {
        const double s = std::sin(M_PI * (k + 1) / n);
        worst = std::fmax(worst, std::fabs(out[k - 1] - s * s));
    }
    std::printf("window: largest difference %.2e\n", worst);
    check(worst < 1e-5, "one grain on a buffer of ones has a Hann window");
    bool gap = true;
    for (int k = n; k < 900; ++k)
        gap = gap && out[k] == 0.f;
    check(gap, "silence between grains that do not overlap");
}

// Many grains: the pool limits them, and the output stays finite.
void testPoolLimit() {
    std::vector<float> data(48000);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<float>(std::sin(i * 0.05));
    const superpos::BufferView buf { data.data(), static_cast<uint32_t>(data.size()), 1, kSampleRate };
    const superpos::Controls c { 0.2f, 200.f, 7.f };
    int active = 0;
    const std::vector<float> out = render(buf, c, 96000, &active);
    bool finite = true;
    for (float v : out)
        finite = finite && std::isfinite(v);
    std::printf("pool: at most %d active grains\n", active);
    check(active <= kMaxGrains, "the active grains stay within the pool");
    check(active == kMaxGrains, "a dense cloud fills the pool");
    check(finite, "a dense cloud gives finite output");
}

// The grain start times of a cloud on a buffer of ones, in samples. The core
// runs one sample at a time, so that each start has its exact time.
std::vector<long> startTimes(const superpos::Controls& c, double seconds, uint32_t seed = 1) {
    static std::vector<float> data(1000, 1.f);
    const superpos::BufferView ones { data.data(), 1000, 1, kSampleRate };
    std::vector<superpos::Grain> pool(kMaxGrains);
    superpos::Core core;
    core.init(pool.data(), kMaxGrains, kSampleRate, kBlock);
    core.seed(seed, seed * 7u + 11u, seed * 13u + 17u);
    float out = 0.f, outRight = 0.f;
    std::vector<long> times;
    core.prime(ones, c, &out, &outRight);
    long started = core.startedGrains();
    const long total = static_cast<long>(seconds * kSampleRate);
    for (long t = 0; t < total; ++t) {
        core.process(ones, c, &out, &outRight, 1);
        if (core.startedGrains() != started)
            times.push_back(t);
        started = core.startedGrains();
    }
    return times;
}

// The coefficient of variation of the intervals between the start times.
double intervalVariation(const std::vector<long>& times) {
    double sum = 0., sum2 = 0.;
    const size_t n = times.size() - 1;
    for (size_t i = 0; i < n; ++i) {
        const double d = static_cast<double>(times[i + 1] - times[i]);
        sum += d;
        sum2 += d * d;
    }
    const double mean = sum / n;
    return std::sqrt(sum2 / n - mean * mean) / mean;
}

bool within(double value, double expected, double part) { return std::fabs(value - expected) <= part * expected; }

// overlap 5 and grainDur 0.05 s give 100 grains a second, 1000 in 10 s.
const superpos::Controls kTiming { 0.05f, 5.f, 0.f, 0.f, 1.f };

void testRegularTiming() {
    const std::vector<long> times = startTimes(kTiming, 10.);
    std::printf("timing: regularity 0: %zu grains in 10 s\n", times.size());
    // A grain each 480 samples; the first at time 0 starts in the primed
    // sample and is not in the list.
    check(times.size() == 999 || times.size() == 1000, "regularity 0: overlap / grainDur grains a second");
    bool fixed = true;
    for (size_t i = 1; i < times.size(); ++i)
        fixed = fixed && std::labs(times[i] - times[i - 1] - 480) <= 1;
    check(fixed, "regularity 0: a fixed interval");
}

void testRandomTiming() {
    for (float regularity : { 0.5f, 1.f }) {
        superpos::Controls c = kTiming;
        c.regularity = regularity;
        const std::vector<long> times = startTimes(c, 10.);
        const double variation = intervalVariation(times);
        std::printf("timing: regularity %.1f: %zu grains in 10 s, interval variation %.3f\n",
                    regularity, times.size(), variation);
        check(within(times.size(), 1000., 0.05), "random timing keeps the mean rate within 5 %");
        // The interval is (1 - r) + r * E with E exponential (standard
        // deviation 1), so its coefficient of variation is r.
        check(within(variation, regularity, 0.1), "the interval variation is the regularity");
    }
}

void testProbability() {
    for (float regularity : { 0.f, 1.f }) {
        superpos::Controls c = kTiming;
        c.regularity = regularity;
        c.probability = 0.5f;
        const std::vector<long> times = startTimes(c, 10.);
        std::printf("timing: probability 0.5, regularity %.0f: %zu grains in 10 s\n", regularity,
                    times.size());
        check(within(times.size(), 500., 0.05), "probability 0.5 starts half of the grains, within 5 %");
    }
    superpos::Controls never = kTiming;
    never.probability = 0.f;
    check(startTimes(never, 1.).empty(), "probability 0 starts no grain");
    never.probability = NAN;
    check(startTimes(never, 1.).empty(), "probability NaN starts no grain");
}

void testSeed() {
    superpos::Controls c = kTiming;
    c.regularity = 1.f;
    c.probability = 0.7f;
    check(startTimes(c, 2., 5) == startTimes(c, 2., 5), "the same seed gives the same grain times");
    check(startTimes(c, 2., 5) != startTimes(c, 2., 6), "another seed gives other grain times");
}

// Controls that give no grains (grainDur 0, an infinite pointer rate, NaN)
// must not stop the cloud for good: valid controls start grains again.
void testRecovery() {
    std::vector<float> data(1000, 1.f);
    const superpos::BufferView ones { data.data(), 1000, 1, kSampleRate };
    std::vector<superpos::Grain> pool(kMaxGrains);
    superpos::Core core;
    core.init(pool.data(), kMaxGrains, kSampleRate, kBlock);
    std::vector<float> block(kBlock), blockRight(kBlock);
    const float inf = INFINITY;
    const superpos::Controls bad[] = {
        { 0.f, 5.f, 0.f },
        { 0.05f, 5.f, 0.f },
        { NAN, NAN, NAN, NAN, NAN },
        { 0.05f, inf, inf },
    };
    const float badPointerRates[] = { 1.f, inf, NAN, 1.f };
    core.prime(ones, bad[0], block.data(), blockRight.data());
    for (int b = 0; b < 4; ++b) {
        core.source().setPointerRate(badPointerRates[b]);
        for (int k = 0; k < 20; ++k)
            core.process(ones, bad[b], block.data(), blockRight.data(), kBlock);
    }
    core.source().setPointerRate(1.f);
    const long before = core.startedGrains();
    float peak = 0.f;
    for (int k = 0; k < 200; ++k) {
        core.process(ones, kTiming, block.data(), blockRight.data(), kBlock);
        for (float v : block)
            peak = std::fmax(peak, std::fabs(v));
    }
    std::printf("recovery: %ld grains after valid controls, peak %.3f\n", core.startedGrains() - before, peak);
    check(core.startedGrains() - before > 20, "valid controls start grains again");
    check(peak > 0.5f, "valid controls give sound again");
}

// The expected window at sample k of a grain of n samples, for a texture.
double expectedWindow(float texture, int k, int n) {
    const double t = 2. * (k + 1) / n;
    const double x = t < 1. ? t : 2. - t;
    const double s = std::sin(M_PI * (k + 1) / n);
    const double hann = s * s;
    if (texture < 0.5f)
        return std::fmin(1., x * 0.5 / (texture + 0.01));
    return x + (texture - 0.5) * 2. * (hann - x);
}

// One grain on a buffer of ones gives its window, for each texture.
void testTextureShapes() {
    std::vector<float> data(1000, 1.f);
    const superpos::BufferView ones { data.data(), 1000, 1, kSampleRate };
    const int n = 480;
    for (float texture : { 0.f, 0.25f, 0.5f, 0.75f, 1.f }) {
        superpos::Controls c = hardLeft({ 0.01f, 0.5f, 0.f });
        c.texture = texture;
        const std::vector<float> out = render(ones, c, 900);
        double worst = 0.;
        for (int k = 1; k < n; ++k)
            worst = std::fmax(worst, std::fabs(out[k - 1] - expectedWindow(texture, k, n)));
        std::printf("texture %.2f: largest difference from the window formula %.2e\n", texture, worst);
        check(worst < 1e-5, "one grain has the window of its texture");
    }
}

// The texture is read when a grain starts: a change during a grain does not
// change that grain, and the next grain has the new window.
void testTextureHeldPerGrain() {
    std::vector<float> data(1000, 1.f);
    const superpos::BufferView ones { data.data(), 1000, 1, kSampleRate };
    superpos::Controls steep = hardLeft({ 0.01f, 0.5f, 0.f });
    steep.texture = 0.f;
    superpos::Controls smooth = steep;
    smooth.texture = 1.f;
    // Grains of 480 samples, one each 960 samples. The first starts in the
    // primed sample, so its window sample k is at out[k - 1]. The second
    // starts at out[960] or one sample later (float rounding of the trigger
    // phase). The texture changes in the third block, during the first grain.
    std::vector<superpos::Grain> pool(kMaxGrains);
    superpos::Core core;
    core.init(pool.data(), kMaxGrains, kSampleRate, kBlock);
    float first = 0.f, firstRight = 0.f;
    core.prime(ones, steep, &first, &firstRight);
    std::vector<float> out;
    std::vector<float> block(kBlock), blockRight(kBlock);
    for (int b = 0; b < 1536 / kBlock; ++b) {
        core.process(ones, b < 2 ? steep : smooth, block.data(), blockRight.data(), kBlock);
        out.insert(out.end(), block.begin(), block.end());
    }
    double firstGrain = 0., secondGrain = 0.;
    for (int k = 1; k < 480; ++k)
        firstGrain = std::fmax(firstGrain, std::fabs(out[k - 1] - expectedWindow(0.f, k, 480)));
    int start = 900;
    while (start < 1000 && out[start] == 0.f)
        ++start;
    check(start == 960 || start == 961, "the second grain starts one interval after the first");
    for (int k = 0; k < 480; ++k)
        secondGrain = std::fmax(secondGrain, std::fabs(out[start + k] - expectedWindow(1.f, k, 480)));
    std::printf("texture change: first grain %.2e from the steep window, second grain %.2e from Hann\n",
                firstGrain, secondGrain);
    check(firstGrain < 1e-5, "a texture change during a grain does not change that grain");
    check(secondGrain < 1e-5, "the next grain has the new texture");
}

// The random values of each grain that starts in `seconds`, on a buffer of
// 2 s of ones.
std::vector<superpos::GrainStart> grainStarts(const superpos::Controls& c, double seconds, float pointerRate) {
    static std::vector<float> data(96000, 1.f);
    const superpos::BufferView ones { data.data(), 96000, 1, kSampleRate };
    std::vector<superpos::Grain> pool(kMaxGrains);
    superpos::Core core;
    core.init(pool.data(), kMaxGrains, kSampleRate, kBlock);
    core.source().setPointerRate(pointerRate);
    core.seed(3, 5, 7);
    float left = 0.f, right = 0.f;
    std::vector<superpos::GrainStart> starts;
    core.prime(ones, c, &left, &right);
    long started = core.startedGrains();
    const long total = static_cast<long>(seconds * kSampleRate);
    for (long t = 0; t < total; ++t) {
        core.process(ones, c, &left, &right, 1);
        if (core.startedGrains() != started)
            starts.push_back(core.lastStart());
        started = core.startedGrains();
    }
    return starts;
}

// With pan and stereoSpread at 0, both channels are the same: the mono sum
// times cos(pi / 4), as GrainBuf with two channels.
void testCentre() {
    std::vector<float> data(48000);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<float>(std::sin(i * 0.05));
    const superpos::BufferView buf { data.data(), static_cast<uint32_t>(data.size()), 1, kSampleRate };
    const superpos::Controls c { 0.05f, 4.f, 7.f };
    std::vector<float> right;
    const std::vector<float> left = render(buf, c, 48000, nullptr, &right, 0.5f);
    const std::vector<float> mono = render(buf, hardLeft(c), 48000, nullptr, nullptr, 0.5f);
    const double centre = std::cos(M_PI / 4.);
    double same = 0., fromMono = 0., peak = 0.;
    for (size_t i = 0; i < left.size(); ++i) {
        same = std::fmax(same, std::fabs(left[i] - right[i]));
        fromMono = std::fmax(fromMono, std::fabs(left[i] - mono[i] * centre));
        peak = std::fmax(peak, std::fabs(left[i]));
    }
    std::printf("centre: left - right %.2e, left - mono * cos(pi/4) %.2e, peak %.3f\n", same, fromMono, peak);
    check(same == 0., "pan 0 and no spread: both channels are the same");
    check(fromMono < 1e-5 && peak > 0.1, "pan 0: each channel is the mono sum times cos(pi / 4)");
}

// One grain at pan 0.5 has the gains cos and sin of (0.5 * 0.5 + 0.5) * pi / 2.
void testPanLaw() {
    std::vector<float> data(1000, 1.f);
    const superpos::BufferView ones { data.data(), 1000, 1, kSampleRate };
    superpos::Controls c { 0.01f, 0.5f, 0.f };
    c.pan = 0.5f;
    std::vector<float> right;
    const std::vector<float> left = render(ones, c, 480, nullptr, &right);
    const std::vector<float> mono = render(ones, hardLeft(c), 480);
    const double angle = 0.75 * M_PI / 2.;
    double worst = 0.;
    for (int k = 0; k < 479; ++k) {
        worst = std::fmax(worst, std::fabs(left[k] - mono[k] * std::cos(angle)));
        worst = std::fmax(worst, std::fabs(right[k] - mono[k] * std::sin(angle)));
    }
    std::printf("pan law: largest difference %.2e\n", worst);
    check(worst < 1e-6, "the pan is equal-power, as GrainBuf with two channels");
}

// The ranges of the random values are those of the TRand UGens of \cloud:
// TRand(-detuneStep, detuneStep), TRand(0, scatter * SampleRate),
// TRand(pan - spread, pan + spread).fold(-1, 1).
void testRandomRanges() {
    superpos::Controls c { 0.05f, 5.f, 0.f };
    c.detuneStep = 0.5f;
    c.scatter = 0.1f;
    c.pan = 0.8f;
    c.stereoSpread = 0.5f;
    const std::vector<superpos::GrainStart> starts = grainStarts(c, 10., 0.f);
    float detuneMin = 1e9f, detuneMax = -1e9f, panMin = 1e9f, panMax = -1e9f;
    double scatterMin = 1e9, scatterMax = -1e9;
    long panAbove = 0;
    for (const superpos::GrainStart& g : starts) {
        detuneMin = std::fmin(detuneMin, g.detune);
        detuneMax = std::fmax(detuneMax, g.detune);
        scatterMin = std::fmin(scatterMin, g.scatter);
        scatterMax = std::fmax(scatterMax, g.scatter);
        panMin = std::fmin(panMin, g.pan);
        panMax = std::fmax(panMax, g.pan);
        panAbove += g.pan > 0.7f;
    }
    const double above = static_cast<double>(panAbove) / starts.size();
    std::printf("ranges: %zu grains; detune [%.3f, %.3f]; scatter [%.0f, %.0f] frames; pan [%.3f, %.3f], "
                "%.2f above 0.7\n",
                starts.size(), detuneMin, detuneMax, scatterMin, scatterMax, panMin, panMax, above);
    check(starts.size() > 900, "the cloud starts grains");
    check(detuneMin >= -0.5f && detuneMax <= 0.5f && detuneMin < -0.45f && detuneMax > 0.45f,
          "the detune covers +-detuneStep semitones");
    check(scatterMin >= 0. && scatterMax < 4800. && scatterMax > 4500.,
          "the scatter covers [0, scatter * sample rate) frames");
    // pan + [-0.5, 0.5] is [0.3, 1.3]; the fold takes (1, 1.3] to [0.7, 1).
    // So 0.3 of the range lands in (0.7, 1] directly and 0.3 by the fold.
    check(panMin >= 0.3f && panMax <= 1.f && panMin < 0.32f && panMax > 0.98f,
          "the pan covers pan +- stereoSpread, folded into [-1, 1]");
    check(std::fabs(above - 0.6) < 0.05, "the fold doubles the density above 0.7");

    superpos::Controls fixed = c;
    fixed.detuneStep = 0.f;
    fixed.scatter = 0.f;
    fixed.stereoSpread = 0.f;
    fixed.pan = -0.5f;
    bool allFixed = true;
    for (const superpos::GrainStart& g : grainStarts(fixed, 1., 0.f))
        allFixed = allFixed && g.detune == 0.f && g.scatter == 0. && g.pan == -0.5f;
    check(allFixed, "controls at 0 give no random values");
}

// White noise in the buffer, so that grains at different read positions are
// not correlated.
std::vector<float> whiteNoise(size_t frames) {
    std::vector<float> data(frames);
    superpos::Random random;
    random.seed(11, 13, 17);
    for (float& v : data)
        v = static_cast<float>(random.uniform() * 2. - 1.);
    return data;
}

double rmsDb(const std::vector<float>& x, size_t from) {
    double sum = 0.;
    for (size_t i = from; i < x.size(); ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return 10. * std::log10(sum / (x.size() - from));
}

// With normalize on, the level of grains that are not correlated stays the
// same from overlap 1 to 60. The pointer moves at half the speed of the
// grains, so that the grains read different parts of the noise. The scatter
// gives each grain a random start between two frames: linear interpolation
// at a fraction f keeps (1 - f)^2 + f^2 of the power of white noise, so
// without it the level would depend on whether the grain interval puts the
// starts on whole or half frames.
void testLevel() {
    const std::vector<float> data = whiteNoise(96000);
    const superpos::BufferView noise { data.data(), 96000, 1, kSampleRate };
    std::vector<double> levels;
    double plainLow = 0., plainHigh = 0.;
    for (float overlap : { 1.f, 2.f, 5.f, 10.f, 20.f, 40.f, 60.f }) {
        superpos::Controls c { 0.05f, overlap, 0.f };
        c.scatter = 0.5f;
        const double level = rmsDb(render(noise, c, 144000, nullptr, nullptr, 0.5f), 48000);
        superpos::Controls plain = c;
        plain.normalize = 0.f;
        const double plainLevel = rmsDb(render(noise, plain, 144000, nullptr, nullptr, 0.5f), 48000);
        std::printf("level: overlap %2.0f: %6.2f dB (without normalize %6.2f dB)\n", overlap, level, plainLevel);
        levels.push_back(level);
        if (overlap == 1.f)
            plainLow = plainLevel;
        plainHigh = plainLevel;
    }
    double mean = 0.;
    for (double l : levels)
        mean += l / levels.size();
    bool flat = true;
    for (double l : levels)
        flat = flat && std::fabs(l - mean) <= 1.5;
    check(flat, "normalize: the level stays within +-1.5 dB from overlap 1 to 60");
    // Without normalize the level rises by about 10 * log10(60) = 17.8 dB.
    check(plainHigh - plainLow > 15., "without normalize the level rises with the overlap");
}

// With normalize 0 the output is the plain sum of the grains: the same as
// the output without the level control (checked against GrainBuf in the
// flux-engine tests). Here: one grain on a buffer of ones has its window.
void testNormalizeOff() {
    std::vector<float> data(1000, 1.f);
    const superpos::BufferView ones { data.data(), 1000, 1, kSampleRate };
    superpos::Controls c = hardLeft({ 0.05f, 30.f, 0.f });
    c.normalize = 0.f;
    const std::vector<float> plain = render(ones, c, 4800);
    c.normalize = 1.f;
    const std::vector<float> scaled = render(ones, c, 4800);
    // 30 grains of 2400 samples on a buffer of ones: the sum of the windows
    // is about 15 after the first grain duration.
    std::printf("normalize off: %.2f at sample 4000 (%.2f with normalize)\n", plain[4000], scaled[4000]);
    check(plain[4000] > 14.f && plain[4000] < 16.f, "normalize 0 gives the plain sum");
    check(scaled[4000] < 4.f, "normalize 1 scales the sum");
}

} // namespace

int main() {
    testNoBuffer();
    testStereoBuffer();
    testWindowShape();
    testPoolLimit();
    testRegularTiming();
    testRandomTiming();
    testProbability();
    testSeed();
    testRecovery();
    testTextureShapes();
    testTextureHeldPerGrain();
    testCentre();
    testPanLaw();
    testRandomRanges();
    testLevel();
    testNormalizeOff();
    std::printf("%s\n", failures ? "superpos_core_test: FAIL" : "superpos_core_test: OK");
    return failures ? 1 : 0;
}
