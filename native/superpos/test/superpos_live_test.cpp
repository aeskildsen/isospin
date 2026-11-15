// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tests of the live source of the SuperPos engine (the SuperPosLive UGen).
// scripts/check-native.py builds them with AddressSanitizer and
// UndefinedBehaviorSanitizer. Exit code 0 is a pass.
#include <algorithm>
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

// Freeze goes on and off while grains play. The grains that would read across
// the head fade out and end before it. The stamps work as in
// testReadStaysBehindHead, but each input sample is the number of the write
// (1, 2, 3, ...), which stops while frozen. So the buffer always holds
// the writes newest - frames + 1 to newest. A grain can read up to the
// newest write before the current sample and back to the oldest write still
// in the buffer. A read across the head can still give a write that is in
// the buffer (from the newest to the oldest, when frozen), so the test also
// checks that the stamp of a grain moves by its rate in each sample: a read
// across the head jumps by about the buffer length.
struct ToggleCase {
    const char* name;
    float grainDur, transpose, position;
};

void testFreezeToggles(const ToggleCase& tc) {
    const uint32_t frames = 4800;
    Live stamps(frames), ones(frames);
    superpos::Controls c { tc.grainDur, 0.5f, tc.transpose };
    c.pan = -1.f;
    c.normalize = 0.f;
    std::vector<float> in(kBlock), unit(kBlock, 1.f);
    float l1[kBlock], r1[kBlock], l2[kBlock], r2[kBlock];
    // About 0.37 s, in whole blocks.
    const long togglePeriod = 278 * kBlock;
    long checked = 0;
    double worstAhead = -1e9, worstOld = -1e9;
    bool finite = true;
    // One grain at a time (overlap 0.5), so consecutive checked samples are
    // from the same grain, and its stamp moves by `rate` in each sample.
    const double rate = std::pow(2., tc.transpose / 12.);
    double previous = NAN, worstStep = 0.;
    // The number of the newest write in the buffer.
    long newest = 0;
    for (long t0 = 0; t0 < 8 * 48000; t0 += kBlock) {
        for (int i = 0; i < kBlock; ++i)
            in[i] = static_cast<float>(newest + 1 + i);
        const bool frozen = t0 >= 2 * static_cast<long>(frames) && (t0 / togglePeriod) % 2 == 1;
        const float freeze = frozen ? 1.f : 0.f;
        stamps.block(in.data(), 1, freeze, tc.position, c, l1, r1);
        ones.block(unit.data(), 1, freeze, tc.position, c, l2, r2);
        // The source writes the whole block before the grains play.
        const long newestAfterBlock = frozen ? newest : newest + kBlock;
        for (int i = 0; i < kBlock; ++i) {
            const long t = t0 + i;
            finite = finite && std::isfinite(l1[i]) && std::isfinite(l2[i]);
            if (t < 2 * static_cast<long>(frames) || l2[i] < 0.01f) {
                previous = NAN;
                continue;
            }
            const double stamp = l1[i] / l2[i];
            if (std::isfinite(previous))
                worstStep = std::fmax(worstStep, std::fabs(stamp - previous - rate));
            previous = stamp;
            // Sample i of the block comes after the writes of samples 0 to
            // i - 1.
            const double newestBefore = frozen ? newest : static_cast<double>(newest + i);
            worstAhead = std::fmax(worstAhead, stamp - newestBefore);
            worstOld = std::fmax(worstOld, static_cast<double>(newestAfterBlock - frames + 1) - stamp);
            ++checked;
        }
        newest = newestAfterBlock;
    }
    std::printf("freeze toggles (%s): %ld samples checked, %ld grains, read at most %.3f frames ahead of the "
                "newest input and %.3f frames older than the buffer (both must be <= 0); step at most %.3f "
                "frames from the rate\n",
                tc.name, checked, stamps.core.startedGrains(), worstAhead, worstOld, worstStep);
    check(finite, "freeze toggles: the output is finite");
    check(checked > 40000, "freeze toggles: the test checks enough samples");
    // The stamps go up to 384000, where the float resolution is about 0.03;
    // a read across the head is off by at least one frame.
    check(worstAhead <= 0.5, "freeze toggles: no grain reads ahead of the record head");
    check(worstOld <= 0.5, "freeze toggles: no grain reads input that the head wrote over");
    check(worstStep <= 0.5, "freeze toggles: no grain jumps across the head");
}

// The shape of the fade-out. One grain of 0.5 s at rate 1 reads `position`
// times the buffer length behind the head; the buffer and the input are ones.
// Freeze goes on during the grain: the head stops and the grain moves
// towards it. A second engine without the freeze plays the same grain, so
// the ratio of the two outputs is the fade gain. The grain must play on at
// gain 1, fade linearly over its last min(240, R) samples (5 ms) and end
// after R samples, where R is the number of samples before it reaches the
// limit (distance - kMinDistance). At position 0 the grain is at the limit
// and stops at once.
void testFadeShape() {
    const uint32_t frames = 4800;
    const long freezeAt = 100 * kBlock;
    for (float position : { 0.5f, 0.01f, 0.f }) {
        Live frozen(frames), open(frames);
        std::fill(frozen.data.begin(), frozen.data.end(), 1.f);
        std::fill(open.data.begin(), open.data.end(), 1.f);
        superpos::Controls c { 0.5f, 0.5f, 0.f };
        c.pan = -1.f;
        c.normalize = 0.f;
        std::vector<float> unit(kBlock, 1.f);
        float l1[kBlock], r1[kBlock], l2[kBlock], r2[kBlock];
        // The distance stays position * frames at rate 1 (place() keeps it
        // at least kMinDistance). In double, as the source: 0.01f is a
        // little less than 0.01, so the distance is a little less than 48.
        const double distance =
            std::fmax(static_cast<double>(position) * frames, superpos::LiveSource::kMinDistance);
        const long remaining = static_cast<long>(std::floor(distance - superpos::LiveSource::kMinDistance));
        const long fade = std::min(240L, remaining);
        double worst = 0.;
        long compared = 0;
        for (long t0 = 0; t0 < freezeAt + 4000; t0 += kBlock) {
            const float freeze = t0 >= freezeAt ? 1.f : 0.f;
            frozen.block(unit.data(), 1, freeze, position, c, l1, r1);
            open.block(unit.data(), 1, 0.f, position, c, l2, r2);
            for (int i = 0; i < kBlock; ++i) {
                const long k = t0 + i - freezeAt;
                if (k < 0 || l2[i] < 0.01f)
                    continue;
                double expected = 0.;
                if (k < remaining - fade)
                    expected = 1.;
                else if (k < remaining)
                    expected = 1. - static_cast<double>(k - (remaining - fade) + 1) / fade;
                worst = std::fmax(worst, std::fabs(l1[i] / l2[i] - expected));
                ++compared;
            }
        }
        std::printf("fade shape: position %.2f: %ld samples before the limit, fade %ld; %ld samples, largest "
                    "difference from the expected gain %.2e\n",
                    position, remaining, fade, compared, worst);
        check(frozen.core.fadedGrains() == 1, "fade shape: the freeze change shortens the grain once");
        check(compared > 3000, "fade shape: the test compares enough samples");
        check(worst < 1e-4, "fade shape: the grain plays on, fades linearly and ends before the limit");
    }
}

// A buffer change moves the head and the limits. One grain of 0.5 s at rate
// 1 reads 2400 frames behind the head in a buffer of 4800 frames. After 100
// blocks the head is at frame 1600 and the grain at frame 4000. Then the
// buffer becomes 2400 frames long (the same memory): the head is at 1600 and
// the grain reads at frame 4000 - 2400 = 1600, on the head. The grain must
// stop at once; without the check it would read the input of the current
// block until it ends.
void testBufferShrink() {
    Live live(4800);
    std::fill(live.data.begin(), live.data.end(), 1.f);
    superpos::Controls c { 0.5f, 0.5f, 0.f };
    c.pan = -1.f;
    c.normalize = 0.f;
    std::vector<float> unit(kBlock, 1.f);
    float l[kBlock], r[kBlock];
    float peakAfter = 0.f;
    for (int b = 0; b < 200; ++b) {
        if (b == 100)
            live.view.frames = 2400;
        live.block(unit.data(), 1, 0.f, 0.5f, c, l, r);
        if (b >= 100)
            for (float v : l)
                peakAfter = std::fmax(peakAfter, std::fabs(v));
    }
    std::printf("buffer shrink: %ld grain stopped, peak after the change %.3f\n", live.core.fadedGrains(),
                peakAfter);
    check(live.core.fadedGrains() == 1, "buffer shrink: the grain on the head stops");
    check(peakAfter == 0.f, "buffer shrink: the grain on the head is silent after the change");
}

// The live source records at the server rate. A grain at transpose 0 reads
// one frame in each sample, also when the sample rate of the buffer is not
// the server rate.
void testRateIgnoresBufferRate() {
    const uint32_t frames = 4800;
    Live stamps(frames), ones(frames);
    stamps.view.sampleRate = 44100.;
    ones.view.sampleRate = 44100.;
    superpos::Controls c { 0.02f, 0.5f, 0.f };
    c.pan = -1.f;
    c.normalize = 0.f;
    std::vector<float> in(kBlock), unit(kBlock, 1.f);
    float l1[kBlock], r1[kBlock], l2[kBlock], r2[kBlock];
    double worst = 0.;
    long pairs = 0;
    double previous = NAN;
    for (long t0 = 0; t0 < 48000; t0 += kBlock) {
        for (int i = 0; i < kBlock; ++i)
            in[i] = static_cast<float>(t0 + i);
        stamps.block(in.data(), 1, 0.f, 0.25f, c, l1, r1);
        ones.block(unit.data(), 1, 0.f, 0.25f, c, l2, r2);
        for (int i = 0; i < kBlock; ++i) {
            const double stamp = l2[i] > 0.01f ? l1[i] / l2[i] : NAN;
            if (t0 >= 2 * static_cast<long>(frames) && std::isfinite(stamp) && std::isfinite(previous)) {
                worst = std::fmax(worst, std::fabs(stamp - previous - 1.));
                ++pairs;
            }
            previous = stamp;
        }
    }
    std::printf("buffer rate 44100 at 48000: %ld sample pairs, the read moves at most %.2e from 1 frame a sample\n",
                pairs, worst);
    check(pairs > 10000, "buffer rate: the test checks enough samples");
    // The stamps near 48000 have a float resolution of about 4e-3. With the
    // rate of the buffer, the read would move 0.08 frames too little.
    check(worst < 0.02, "a grain at transpose 0 reads one frame in each sample at any buffer rate");
}

// A grain at rate 1 starts `position` times the buffer length behind the
// head, and stays at that distance.
void testPosition() {
    const uint32_t frames = 4800;
    for (float position : { 0.25f, 0.5f, 0.75f }) {
        Live stamps(frames), ones(frames);
        superpos::Controls c { 0.02f, 0.5f, 0.f };
        c.pan = -1.f;
        c.normalize = 0.f;
        std::vector<float> in(kBlock), unit(kBlock, 1.f);
        float l1[kBlock], r1[kBlock], l2[kBlock], r2[kBlock];
        double worst = 0.;
        long n = 0;
        for (long t0 = 0; t0 < 48000; t0 += kBlock) {
            for (int i = 0; i < kBlock; ++i)
                in[i] = static_cast<float>(t0 + i);
            stamps.block(in.data(), 1, 0.f, position, c, l1, r1);
            ones.block(unit.data(), 1, 0.f, position, c, l2, r2);
            for (int i = 0; i < kBlock; ++i) {
                const long t = t0 + i;
                if (t < 2 * static_cast<long>(frames) || l2[i] < 0.01f)
                    continue;
                // Frame head - 1 holds input t - 1, so a read d frames
                // behind the head reads input t - d.
                const double distance = t - l1[i] / l2[i];
                worst = std::fmax(worst, std::fabs(distance - position * frames));
                ++n;
            }
        }
        std::printf("position %.2f: %ld samples, distance at most %.3f frames from %.0f\n", position, n, worst,
                    position * frames);
        check(n > 10000, "position: the test checks enough samples");
        check(worst < 1.01, "position: a grain reads at position * frames behind the head");
    }
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
    for (const ToggleCase& tc : { ToggleCase { "position 0, transpose 0", 0.05f, 0.f, 0.f },
                                  ToggleCase { "position 0, transpose 12", 0.05f, 12.f, 0.f },
                                  ToggleCase { "position 1, transpose -12", 0.05f, -12.f, 1.f },
                                  ToggleCase { "position 0, grainDur 0.5", 0.5f, 0.f, 0.f },
                                  ToggleCase { "position 0.5, transpose 7", 0.05f, 7.f, 0.5f } })
        testFreezeToggles(tc);
    testFadeShape();
    testBufferShrink();
    testRateIgnoresBufferRate();
    testPosition();
    testRecordAndFreeze();
    testConstantInput();
    testNoRoom();
    std::printf("%s\n", failures ? "superpos_live_test: FAIL" : "superpos_live_test: OK");
    return failures ? 1 : 0;
}
