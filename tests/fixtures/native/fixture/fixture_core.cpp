// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "fixture_core.h"

namespace isospin_fixture {

void apply_gain(const float* in, float* out, int count, float gain) {
    for (int i = 0; i < count; ++i)
        out[i] = in[i] * gain;
}

} // namespace isospin_fixture
