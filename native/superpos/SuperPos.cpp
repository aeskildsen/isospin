// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The SuperPos UGen: overlapping grains from a mono buffer. This file is the
// SuperCollider wrapper; the DSP is in superpos_core.cpp.
//
// Inputs (all read once in each block):
//   0 bufnum       the buffer (one channel)
//   1 pointerRate  the speed of the read pointer, 1 = the speed of the buffer
//   2 grainDur     the grain duration in seconds
//   3 overlap      grains at the same time, on average
//   4 transpose    semitones
//   5 regularity   0 = a fixed grain interval, 1 = random intervals
//   6 probability  the chance that a trigger starts a grain
//   7 texture      the window shape: 0 = a steep trapezoid, 0.5 = a triangle,
//                  1 = a Hann window; read when a grain starts
//
// A SynthDef with fewer inputs gets the default value of each missing input.
#include <SC_PlugIn.hpp>

#include <cmath>

#include <isospin/sc_plugin_table.h>

#include "superpos_core.h"

namespace {

// The size of the grain pool.
constexpr int kMaxGrains = 64;

class SuperPos : public SCUnit {
public:
    SuperPos() {
        SuperPos* unit = this;
        m_fbufnum = -1e9f;
        m_buf = nullptr;
        mPool = static_cast<superpos::Grain*>(RTAlloc(mWorld, kMaxGrains * sizeof(superpos::Grain)));
        ClearUnitIfMemFailed(mPool);

        mCore.init(mPool, kMaxGrains, sampleRate(), bufferSize());
        // The random generator of the synth: RandSeed sets it.
        RGen& rgen = *mParent->mRGen;
        const uint32 a = rgen.trand();
        const uint32 b = rgen.trand();
        const uint32 c = rgen.trand();
        mCore.seed(a, b, c);
        mCalcFunc = make_calc_function<SuperPos, &SuperPos::next>();

        // The first output sample, as the UGens that the core copies make it
        // in their constructors (see superpos::Core::prime).
        run<true>(1);
    }

    ~SuperPos() {
        if (mPool)
            RTFree(mWorld, mPool);
    }

    // The buffer macros of SuperCollider read these two fields.
    float m_fbufnum;
    SndBuf* m_buf;

private:
    float input(int index, float otherwise) const { return numInputs() > index ? in0(index) : otherwise; }

    superpos::Controls controls() const {
        return { in0(1), in0(2), in0(3), in0(4), input(5, 0.f), input(6, 1.f), input(7, 1.f) };
    }

    void next(int numSamples) { run<false>(numSamples); }

    template <bool Prime> void run(int numSamples) {
        SuperPos* unit = this;
        // A buffer number that is not a number, or too large for an int,
        // gives silence.
        if (!(std::fabs(in0(0)) < 2.0e9f)) {
            ClearUnitOutputs(unit, numSamples);
            return;
        }
        // Gets the buffer of input 0 and holds its lock until the end of the
        // block.
        GET_BUF_SHARED
        const superpos::BufferView view { bufData, bufFrames, bufChannels, buf->samplerate };
        if (Prime)
            mCore.prime(view, controls(), out(0));
        else
            mCore.process(view, controls(), out(0), numSamples);
    }

    superpos::Grain* mPool = nullptr;
    superpos::Core mCore;
};

} // namespace

void SuperPos_Load(InterfaceTable* inTable) {
    ISOSPIN_KEEP_TABLE(inTable);
    registerUnit<SuperPos>(inTable, "SuperPos");
}
