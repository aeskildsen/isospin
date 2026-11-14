// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests of the SuperPos DSP core. scripts/check-native.py builds them with
// AddressSanitizer and UndefinedBehaviorSanitizer. Exit code 0 is a pass.
#include <cmath>
#include <cstdio>
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

// Runs the core as the UGen does: one primed sample, then whole blocks.
// Returns `total` samples, the primed sample first.
std::vector<float> render(const superpos::BufferView& buf, const superpos::Controls& c, int total,
                          int* maxActive = nullptr) {
    std::vector<superpos::Grain> pool(kMaxGrains);
    superpos::Core core;
    core.init(pool.data(), kMaxGrains, kSampleRate, kBlock);
    std::vector<float> out(1 + total);
    core.prime(buf, c, out.data());
    int most = core.activeGrains();
    // The primed sample is the output of the constructor; the first block
    // starts again at index 0 of the server's buffer.
    std::vector<float> block(kBlock);
    std::vector<float> result;
    result.reserve(total);
    while (static_cast<int>(result.size()) < total) {
        core.process(buf, c, block.data(), kBlock);
        if (core.activeGrains() > most)
            most = core.activeGrains();
        result.insert(result.end(), block.begin(), block.end());
    }
    result.resize(total);
    if (maxActive)
        *maxActive = most;
    return result;
}

void testNoBuffer() {
    const superpos::BufferView none { nullptr, 0, 1, kSampleRate };
    const superpos::Controls c { 1.f, 0.01f, 4.f, 0.f };
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
    const superpos::Controls c { 1.f, 0.01f, 4.f, 0.f };
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
    const superpos::Controls c { 1.f, 0.01f, 0.5f, 0.f };
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
    const superpos::Controls c { 1.f, 0.2f, 200.f, 7.f };
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

} // namespace

int main() {
    testNoBuffer();
    testStereoBuffer();
    testWindowShape();
    testPoolLimit();
    std::printf("%s\n", failures ? "superpos_core_test: FAIL" : "superpos_core_test: OK");
    return failures ? 1 : 0;
}
