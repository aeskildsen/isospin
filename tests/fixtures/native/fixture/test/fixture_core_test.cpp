// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Test of the IsoFixture DSP core. scripts/check-native.py builds it with
// AddressSanitizer and UndefinedBehaviorSanitizer. Exit code 0 is a pass.
#include <cstdio>
#include <vector>

#include "fixture_core.h"

int main() {
    int failures = 0;

    std::vector<float> in = { 1.0f, -0.5f, 0.25f, 0.0f };
    std::vector<float> out(in.size());
    isospin_fixture::apply_gain(in.data(), out.data(), static_cast<int>(in.size()), 2.0f);
    const float expected[] = { 2.0f, -1.0f, 0.5f, 0.0f };
    for (size_t i = 0; i < in.size(); ++i) {
        if (out[i] != expected[i]) {
            std::printf("FAIL: sample %zu is %g, expected %g\n", i, out[i], expected[i]);
            ++failures;
        }
    }

    // The same buffer for input and output.
    isospin_fixture::apply_gain(in.data(), in.data(), static_cast<int>(in.size()), 0.5f);
    if (in[0] != 0.5f) {
        std::printf("FAIL: in-place sample 0 is %g, expected 0.5\n", in[0]);
        ++failures;
    }

    // Zero samples: no read and no write.
    isospin_fixture::apply_gain(nullptr, nullptr, 0, 1.0f);

    std::printf("%s\n", failures ? "fixture_core_test: FAIL" : "fixture_core_test: OK");
    return failures ? 1 : 0;
}
