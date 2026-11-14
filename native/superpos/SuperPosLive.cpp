// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The SuperPosLive UGen: the SuperPos grain engine on a circular record
// buffer. The UGen writes its input into the buffer, and the grains read the
// recent input behind the record head. This file is the SuperCollider
// wrapper; the DSP is in superpos_core.cpp (LiveSource).
//
// Inputs (all read once in each block):
//   0 bufnum       the record buffer (one channel), from the language
//   1 in           the input; at audio rate, or one value for each block
//   2 freeze       >= 0.5: stop the recording and keep the buffer
//   3 position     where grains start: 0 = the newest input, 1 = the oldest
//                  (a part of the buffer length behind the record head)
//   4 grainDur ... 14 normalize: the engine controls, as SuperPos inputs
//                  2 to 12 (see superpos_unit.h)
//
// Outputs: 0 left, 1 right.
#include "superpos_unit.h"

namespace {

class SuperPosLive : public superpos::GrainUnit<superpos::LiveCore> {
public:
    SuperPosLive() {
        if (!mPool) {
            failed();
            return;
        }
        mCalcFunc = make_calc_function<SuperPosLive, &SuperPosLive::next>();
        run<true>(1);
    }

private:
    void next(int numSamples) { run<false>(numSamples); }

    template <bool Prime> void run(int numSamples) {
        SuperPosLive* unit = this;
        if (badBufferNumber()) {
            ClearUnitOutputs(unit, numSamples);
            return;
        }
        // Gets the buffer of input 0 and holds its lock (not shared: the UGen
        // writes to the buffer) until the end of the block.
        GET_BUF
        const superpos::BufferView view { bufData, bufFrames, bufChannels, buf->samplerate };
        const int inStep = inRate(1) == calc_FullRate ? 1 : 0;
        mCore.source().setInput(in(1), inStep, bufData, in0(2), in0(3));
        if (Prime)
            mCore.prime(view, controls(4), out(0), out(1));
        else
            mCore.process(view, controls(4), out(0), out(1), numSamples);
    }
};

} // namespace

void SuperPosLive_Register(InterfaceTable* inTable) {
    ISOSPIN_KEEP_TABLE(inTable);
    registerUnit<SuperPosLive>(inTable, "SuperPosLive");
}
