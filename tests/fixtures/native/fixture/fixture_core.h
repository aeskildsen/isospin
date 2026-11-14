// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The DSP core of the IsoFixture test UGen: a gain. It uses no SuperCollider
// headers, so that a test program can use it without a server.
#pragma once

namespace isospin_fixture {

// Multiplies `count` samples of `in` by `gain` and writes them to `out`.
// `in` and `out` can be the same buffer.
void apply_gain(const float* in, float* out, int count, float gain);

} // namespace isospin_fixture
