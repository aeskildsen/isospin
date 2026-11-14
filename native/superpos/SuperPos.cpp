// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The SuperPos UGen: overlapping grains from a mono buffer, in two channels. This file is the
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
//   8 detuneStep   each grain is detuned by a random value in +-detuneStep
//                  semitones
//   9 scatter      each grain starts a random time in [0, scatter) seconds
//                  after the pointer
//  10 pan          -1 = left, 1 = right
//  11 stereoSpread each grain pans to a random value in pan +- stereoSpread
//  12 normalize    1 = scale the output by 1 / sqrt(active grains), 0 = off
//
// Outputs: 0 left, 1 right.
//
// A SynthDef with fewer inputs gets the default value of each missing input.
#include "superpos_unit.h"

// Registers SuperPosLive (SuperPosLive.cpp).
void SuperPosLive_Register(InterfaceTable* inTable);

namespace {

class SuperPos : public superpos::GrainUnit<superpos::Core> {
public:
    SuperPos() {
        if (!mPool) {
            failed();
            return;
        }
        mCalcFunc = make_calc_function<SuperPos, &SuperPos::next>();
        // The first output sample, as the UGens that the core copies make it
        // in their constructors (see superpos::Engine::prime).
        run<true>(1);
    }

private:
    void next(int numSamples) { run<false>(numSamples); }

    template <bool Prime> void run(int numSamples) {
        SuperPos* unit = this;
        // A buffer number that is not a number, or too large for an int,
        // gives silence.
        if (badBufferNumber()) {
            ClearUnitOutputs(unit, numSamples);
            return;
        }
        // Gets the buffer of input 0 and holds its lock until the end of the
        // block.
        GET_BUF_SHARED
        const superpos::BufferView view { bufData, bufFrames, bufChannels, buf->samplerate };
        mCore.source().setPointerRate(in0(1));
        if (Prime)
            mCore.prime(view, controls(2), out(0), out(1));
        else
            mCore.process(view, controls(2), out(0), out(1), numSamples);
    }
};

} // namespace

void SuperPos_Load(InterfaceTable* inTable) {
    ISOSPIN_KEEP_TABLE(inTable);
    registerUnit<SuperPos>(inTable, "SuperPos");
    SuperPosLive_Register(inTable);
}
