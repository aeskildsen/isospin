// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A fuzz test of the SuperPos DSP core. It runs the core with random and
// extreme controls and buffers: zero, negative and very large values, NaN and
// infinity, an empty buffer, a buffer of one frame, a buffer with two
// channels, and a change of buffer while grains play. For each parameter set
// it checks that:
//
//   - each output sample is finite;
//   - no output sample is larger than the pool size times the buffer peak
//     (each grain adds at most the buffer peak; the core has no level
//     control).
//
// scripts/check-native.py builds it with AddressSanitizer and
// UndefinedBehaviorSanitizer, which stop it at a bad memory access or
// undefined behaviour.
//
// Usage: superpos_fuzz [sets] [seed]
// The defaults (1000000 sets, seed 1) are the run of the build checks. A
// different seed or more sets give a longer run.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include "superpos_core.h"

namespace {

constexpr double kSampleRate = 48000.;
constexpr int kBlock = 64;
constexpr int kMaxGrains = 64;

// A small random generator (SplitMix64). Its sequence is the same on all
// platforms, so a seed gives the same run everywhere.
class Dice {
public:
    explicit Dice(uint64_t seed) : mState(seed) {}

    uint64_t next() {
        uint64_t z = (mState += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    // An integer in [0, n).
    int below(int n) { return static_cast<int>(next() % static_cast<uint64_t>(n)); }

    // A value in [0, 1).
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

    double range(double lo, double hi) { return lo + (hi - lo) * unit(); }

private:
    uint64_t mState;
};

const float kInf = std::numeric_limits<float>::infinity();
const float kNaN = std::numeric_limits<float>::quiet_NaN();

const float kExtremes[] = {
    0.f,
    -0.f,
    1e-45f, // the smallest denormal
    -1e-45f,
    1e-30f,
    -1e-30f,
    1e-7f,
    -1.f,
    1.f,
    4.f,
    1e4f,
    -1e4f,
    1e30f,
    -1e30f,
    std::numeric_limits<float>::max(),
    std::numeric_limits<float>::lowest(),
    kInf,
    -kInf,
    kNaN,
};

// Half of the time an extreme value, else a value in the normal range.
float control(Dice& dice, double lo, double hi) {
    if (dice.below(2) == 0)
        return kExtremes[dice.below(sizeof(kExtremes) / sizeof(kExtremes[0]))];
    return static_cast<float>(dice.range(lo, hi));
}

superpos::Controls randomControls(Dice& dice) {
    superpos::Controls c;
    c.pointerRate = control(dice, -4., 4.);
    c.grainDur = control(dice, 0.001, 1.);
    c.overlap = control(dice, 0., 80.);
    c.transpose = control(dice, -48., 48.);
    c.regularity = control(dice, 0., 1.);
    c.probability = control(dice, 0., 1.);
    c.texture = control(dice, 0., 1.);
    return c;
}

// A buffer and the largest absolute value in it.
struct TestBuffer {
    std::vector<float> data;
    superpos::BufferView view;
    float peak;
};

// One of the buffer kinds: no buffer, no frames, one frame, two frames, two
// channels, or a random mono buffer of up to 4096 frames. The content is
// finite and at most 1 in magnitude; the sample rate is sometimes extreme.
void randomBuffer(Dice& dice, TestBuffer& b) {
    const int kind = dice.below(8);
    uint32_t frames = 0, channels = 1;
    switch (kind) {
    case 0: // no buffer
        b.data.clear();
        b.view = { nullptr, 0, 1, kSampleRate };
        b.peak = 0.f;
        return;
    case 1:
        frames = 0;
        break;
    case 2:
        frames = 1;
        break;
    case 3:
        frames = 2;
        break;
    case 4:
        frames = 1 + dice.below(512);
        channels = 2;
        break;
    default:
        frames = 1 + dice.below(4096);
        break;
    }
    // New memory of the exact size for each buffer, so that AddressSanitizer
    // sees a read past its end (a reused vector can have more capacity).
    b.data = std::vector<float>(static_cast<size_t>(frames) * channels);
    b.peak = 0.f;
    for (float& v : b.data) {
        v = static_cast<float>(dice.range(-1., 1.));
        b.peak = std::fmax(b.peak, std::fabs(v));
    }
    double rate = kSampleRate;
    switch (dice.below(8)) {
    case 0:
        rate = 0.;
        break;
    case 1:
        rate = static_cast<double>(control(dice, 8000., 192000.));
        break;
    case 2:
        rate = 44100.;
        break;
    default:
        break;
    }
    // A buffer with no frames still has a valid pointer, as an allocated
    // buffer of zero frames in the server.
    b.view = { frames ? b.data.data() : reinterpret_cast<const float*>(&b.peak), frames, channels, rate };
}

// Counts that show that the run reached the grain code, not only the early
// exits.
struct Coverage {
    long setsWithGrains = 0;
    long setsWithFullPool = 0;
    long grains = 0;
};

struct Failure {
    long set;
    const char* what;
    float value;
};

// Runs one parameter set: a few blocks with one buffer, then a few blocks
// with another buffer while the grains of the first one still play. The
// controls change between the blocks.
bool runSet(Dice& dice, superpos::Grain* pool, TestBuffer& first, TestBuffer& second, long set,
            Coverage& coverage, Failure& failure) {
    superpos::Core core;
    core.init(pool, kMaxGrains, kSampleRate, kBlock);
    core.seed(static_cast<uint32_t>(dice.next()), static_cast<uint32_t>(dice.next()),
              static_cast<uint32_t>(dice.next()));
    randomBuffer(dice, first);
    randomBuffer(dice, second);

    float out[kBlock];
    const auto checkBlock = [&](int n, float peak) {
        const float limit = kMaxGrains * peak * 1.0001f;
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(out[i])) {
                failure = { set, "output is not finite", out[i] };
                return false;
            }
            if (std::fabs(out[i]) > limit) {
                failure = { set, "output is larger than the pool size times the buffer peak", out[i] };
                return false;
            }
        }
        return true;
    };

    superpos::Controls c = randomControls(dice);
    core.prime(first.view, c, out);
    if (!checkBlock(1, first.peak))
        return false;
    bool fullPool = core.activeGrains() == kMaxGrains;
    const int blocks = 1 + dice.below(6);
    for (int k = 0; k < 2 * blocks; ++k) {
        const TestBuffer& b = k < blocks ? first : second;
        // The output can hold grains of both buffers in the first block after
        // the change.
        const float peak = std::fmax(first.peak, second.peak);
        if (dice.below(3) == 0)
            c = randomControls(dice);
        const int n = 1 + dice.below(kBlock);
        core.process(b.view, c, out, n);
        if (!checkBlock(n, peak))
            return false;
        if (core.activeGrains() < 0 || core.activeGrains() > kMaxGrains) {
            failure = { set, "the active grain count is outside the pool", static_cast<float>(core.activeGrains()) };
            return false;
        }
        fullPool = fullPool || core.activeGrains() == kMaxGrains;
    }
    coverage.grains += core.startedGrains();
    coverage.setsWithGrains += core.startedGrains() > 0;
    coverage.setsWithFullPool += fullPool;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const long sets = argc > 1 ? std::atol(argv[1]) : 1000000;
    const uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
    Dice dice(seed);
    std::vector<superpos::Grain> pool(kMaxGrains);
    TestBuffer first, second;
    Coverage coverage;
    Failure failure {};
    for (long set = 0; set < sets; ++set) {
        if (!runSet(dice, pool.data(), first, second, set, coverage, failure)) {
            std::printf("superpos_fuzz: FAIL at set %ld (seed %llu): %s (%g)\n", failure.set,
                        static_cast<unsigned long long>(seed), failure.what, failure.value);
            return 1;
        }
    }
    std::printf("superpos_fuzz: %ld grains; %ld sets with grains, %ld with a full pool\n", coverage.grains,
                coverage.setsWithGrains, coverage.setsWithFullPool);
    std::printf("superpos_fuzz: OK (%ld sets, seed %llu)\n", sets, static_cast<unsigned long long>(seed));
    return 0;
}
