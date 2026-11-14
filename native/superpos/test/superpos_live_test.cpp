// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests of the live source of the SuperPos engine (the SuperPosLive UGen).
// scripts/check-native.py builds them with AddressSanitizer and
// UndefinedBehaviorSanitizer. Exit code 0 is a pass.
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

// An engine with the live source, its pool and its buffer.
struct Live {
    std::vector<superpos::Grain> pool = std::vector<superpos::Grain>(kMaxGrains);
    std::vector<float> data;
    superpos::LiveCore core;
    superpos::BufferView view {};

    explicit Live(uint32_t frames) : data(frames, 0.f) {
        core.init(pool.data(), kMaxGrains, kSampleRate, kBlock);
        core.seed(21, 22, 23);
        view = { data.data(), frames, 1, kSampleRate };
    }

    // One block with the input `in`.
    void block(const float* in, int inStep, float freeze, float position, const superpos::Controls& c,
               float* left, float* right, int n = kBlock) {
        core.source().setInput(in, inStep, data.data(), freeze, position);
        core.process(view, c, left, right, n);
    }
};

// The controls of the tests: one grain at a time (overlap 0.5), all of it on
// the left channel with a gain of 1, no level control. A detune of up to an
// octave each way gives read rates from 0.5 to 2.
superpos::Controls testControls() {
    superpos::Controls c { 0.02f, 0.5f, 0.f };
    c.pan = -1.f;
    c.normalize = 0.f;
    c.detuneStep = 12.f;
    c.scatter = 0.05f;
    return c;
}

// A grain must not read across the record head. The input is the time in
// samples, so a frame holds the time when it was written. A second engine
// with the same seed records an input of ones; it has the same grains, and
// its output is the window. Their ratio is the time stamp that the grain
// reads, which must be older than the current sample and not older than one
// buffer length. Freeze goes on after 1 s. Two cases: the read position
// moves and the grains scatter (`near` false), or the grains start as near
// to the head as they can (`near` true: position 0, no scatter).
void testReadStaysBehindHead(bool near) {
    const uint32_t frames = 4800;
    Live stamps(frames), ones(frames);
    superpos::Controls c = testControls();
    if (near)
        c.scatter = 0.f;
    std::vector<float> in(kBlock), unit(kBlock, 1.f);
    float l1[kBlock], r1[kBlock], l2[kBlock], r2[kBlock];
    const long freezeAt = 48000;
    // Grains that start before the freeze can play over it; their limit
    // holds only for the head speed at their start.
    const long settle = freezeAt + 2 * 960;
    long checked = 0, frozenChecked = 0;
    double worstAhead = -1e9, worstOld = -1e9;
    bool frozenInRange = true;
    for (long t0 = 0; t0 < 96000; t0 += kBlock) {
        for (int i = 0; i < kBlock; ++i)
            in[i] = static_cast<float>(t0 + i);
        const float freeze = t0 >= freezeAt ? 1.f : 0.f;
        const float position = near ? 0.f : static_cast<float>((t0 / kBlock) % 37) / 37.f;
        stamps.block(in.data(), 1, freeze, position, c, l1, r1);
        ones.block(unit.data(), 1, freeze, position, c, l2, r2);
        for (int i = 0; i < kBlock; ++i) {
            const long t = t0 + i;
            if (t < 2 * static_cast<long>(frames) || l2[i] < 0.05f)
                continue;
            const double stamp = l1[i] / l2[i];
            if (t < freezeAt) {
                // Newest allowed: written at t - 1. Oldest allowed: still in
                // the buffer, written at t - frames + 1.
                worstAhead = std::fmax(worstAhead, stamp - (t - 1));
                worstOld = std::fmax(worstOld, (t - static_cast<double>(frames) + 1) - stamp);
                ++checked;
            } else if (t >= settle) {
                // The buffer holds the input from before the freeze.
                frozenInRange = frozenInRange && stamp <= freezeAt - 1 + 0.5
                                && stamp >= freezeAt - static_cast<double>(frames) - 0.5;
                ++frozenChecked;
            }
        }
    }
    std::printf("head (%s): %ld samples checked, read at most %.3f frames ahead of the newest input and %.3f "
                "frames older than the buffer (both must be <= 0); %ld frozen samples\n",
                near ? "near" : "moving", checked, worstAhead, worstOld, frozenChecked);
    // One grain at a time with overlap 0.5: about half of the samples.
    check(checked > 12000 && frozenChecked > 12000, "the test checks enough samples");
    if (near)
        check(worstAhead > -20., "near: the grains read close to the head");
    check(worstAhead <= 0.01, "no grain reads ahead of the record head");
    check(worstOld <= 0.01, "no grain reads input that the head wrote over");
    check(frozenInRange, "frozen: the grains read only the input from before the freeze");
}

// The input goes into the buffer; freeze stops it and keeps the content.
void testRecordAndFreeze() {
    Live live(1000);
    const superpos::Controls c = testControls();
    float l[kBlock], r[kBlock];
    std::vector<float> in(kBlock);
    for (int b = 0; b < 4; ++b) {
        for (int i = 0; i < kBlock; ++i)
            in[i] = static_cast<float>(b * kBlock + i + 1);
        live.block(in.data(), 1, 0.f, 0.f, c, l, r);
    }
    bool recorded = true;
    for (int k = 0; k < 4 * kBlock; ++k)
        recorded = recorded && live.data[k] == static_cast<float>(k + 1);
    check(recorded, "the input goes into the buffer, frame after frame");
    check(live.core.source().head() == 4 * kBlock, "the head is after the newest input");

    const std::vector<float> before = live.data;
    for (int b = 0; b < 40; ++b)
        live.block(in.data(), 1, 1.f, 0.f, c, l, r);
    check(live.data == before, "freeze keeps the buffer");
    check(live.core.source().head() == 4 * kBlock, "freeze stops the head");

    // The head goes on from where it stopped, and wraps at the end.
    for (int b = 0; b < 20; ++b)
        live.block(in.data(), 1, 0.f, 0.f, c, l, r);
    check(live.core.source().head() == (24 * kBlock) % 1000, "the head wraps at the end of the buffer");
}

// An input that is not at audio rate is one value for the whole block.
void testConstantInput() {
    Live live(200);
    const superpos::Controls c = testControls();
    float l[kBlock], r[kBlock];
    const float value = 0.25f;
    live.block(&value, 0, 0.f, 0.f, c, l, r);
    bool constant = true;
    for (int k = 0; k < kBlock; ++k)
        constant = constant && live.data[k] == value;
    check(constant, "a control-rate input fills the block with its value");
}

// A grain that cannot stay behind the head in the buffer does not start.
// Rate 2 (an octave up) for 480 samples moves 480 frames towards the head; a
// buffer of 300 frames has no room for that. At rate 1 the grain moves with
// the head and fits in any buffer.
void testNoRoom() {
    for (float transpose : { 12.f, 0.f }) {
        Live live(300);
        superpos::Controls c { 0.01f, 1.f, transpose };
        float l[kBlock], r[kBlock];
        std::vector<float> in(kBlock, 1.f);
        for (int b = 0; b < 100; ++b)
            live.block(in.data(), 1, 0.f, 0.5f, c, l, r);
        std::printf("no room: transpose %.0f: %ld grains\n", transpose, live.core.startedGrains());
        if (transpose > 0.f)
            check(live.core.startedGrains() == 0, "no grain starts when it cannot stay behind the head");
        else
            check(live.core.startedGrains() > 0, "at rate 1 a grain fits in a small buffer");
    }
}

} // namespace

int main() {
    testReadStaysBehindHead(false);
    testReadStaysBehindHead(true);
    testRecordAndFreeze();
    testConstantInput();
    testNoRoom();
    std::printf("%s\n", failures ? "superpos_live_test: FAIL" : "superpos_live_test: OK");
    return failures ? 1 : 0;
}
