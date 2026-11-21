// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The Pulsar UGen: pulsar synthesis with three trains of sine grains, in two
// channels. This file is the SuperCollider wrapper; the DSP is in
// pulsar_core.cpp.
//
// Inputs (all read once in each block):
//    0 freq      trigger frequency in Hz
//    1 energy    energy of the note, 0 to 1 (usually an envelope)
//    2 velocity  0 to 1, louder and brighter
//    3 formant1  grain frequency of train 1 / freq
//    4 formant2  grain frequency of train 2 / freq
//    5 overlap0  grain duration of train 0 in periods of freq
//    6 overlap1  the same for train 1
//    7 overlap2  the same for train 2
//    8 wobble    depth of the LFOs
//    9 detune    trigger offsets of trains 1 and 2, in Hz
//   10 jitter    amount of pink noise on energy
//   11 law       how the grain durations follow energy
//   12 width     stereo width
//
// Outputs: 0 left, 1 right.
//
// A SynthDef with fewer inputs gets the default value of each missing input.
// A unit that does not have two outputs gives silence and a message.
#include <SC_PlugIn.hpp>

#include <isospin/sc_plugin_table.h>

#include "pulsar_core.h"

namespace {

class Pulsar : public SCUnit {
public:
    Pulsar() {
        Unit* unit = this;
        if (numOutputs() != 2) {
            Print("IsoPulsar: the SynthDef has an IsoPulsar UGen with %d outputs; it needs 2. Make the SynthDef again "
                  "with the current class file.\n",
                  static_cast<int>(numOutputs()));
            SETCALC(ClearUnitOutputs);
            ClearUnitOutputs(unit, 1);
            unit->mDone = true;
            return;
        }
        mCore.init(sampleRate());
        // The random generator of the synth, as Rand: RandSeed sets it.
        RGen& rgen = *mParent->mRGen;
        const uint32 a = rgen.trand();
        const uint32 b = rgen.trand();
        const uint32 c = rgen.trand();
        mCore.seed(a, b, c);
        mCalcFunc = make_calc_function<Pulsar, &Pulsar::next>();
        // The first sample is silent: the first grains start in the first
        // block.
        out(0)[0] = 0.f;
        out(1)[0] = 0.f;
    }

private:
    float input(int index, float otherwise) const { return static_cast<int>(numInputs()) > index ? in0(index) : otherwise; }

    void next(int numSamples) {
        pulsar::Controls c;
        c.freq = input(0, c.freq);
        c.energy = input(1, c.energy);
        c.velocity = input(2, c.velocity);
        c.formant1 = input(3, c.formant1);
        c.formant2 = input(4, c.formant2);
        c.overlap0 = input(5, c.overlap0);
        c.overlap1 = input(6, c.overlap1);
        c.overlap2 = input(7, c.overlap2);
        c.wobble = input(8, c.wobble);
        c.detune = input(9, c.detune);
        c.jitter = input(10, c.jitter);
        c.law = input(11, c.law);
        c.width = input(12, c.width);
        mCore.process(c, out(0), out(1), numSamples);
    }

    pulsar::Core mCore;
};

} // namespace

void Pulsar_Load(InterfaceTable* inTable) {
    ISOSPIN_KEEP_TABLE(inTable);
    registerUnit<Pulsar>(inTable, "IsoPulsar");
}
