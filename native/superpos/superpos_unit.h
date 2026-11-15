// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The parts that the SuperPos UGens share: the grain pool, the engine and
// its random seed, and the inputs of the engine controls.
//
// The engine controls are 11 inputs after the inputs of the source, in this
// order: grainDur, overlap, transpose, regularity, probability, texture,
// detuneStep, scatter, pan, stereoSpread, normalize. A SynthDef with fewer
// inputs gets the default value of each missing input after transpose. A
// SynthDef with a unit that does not have two outputs (from a class file
// before the stereo output), or that does not have the inputs up to
// transpose, gives silence and a message. After the engine controls comes maxGrains,
// the size of the grain pool, read once when the UGen starts.
#pragma once

#include <SC_PlugIn.hpp>

#include <cmath>

#include <isospin/sc_plugin_table.h>

#include "superpos_core.h"

namespace superpos {

template <class Engine> class GrainUnit : public SCUnit {
public:
    // The buffer macros of SuperCollider read these two fields.
    float m_fbufnum = -1e9f;
    SndBuf* m_buf = nullptr;

protected:
    // The pool size when the input is not there, and the limits of the input.
    static constexpr int kDefaultGrains = 64;
    static constexpr int kMaxGrains = 1024;

    // Gets the pool from the real-time memory and seeds the engine. The pool
    // size is input `maxGrainsInput`. The unit needs `minInputs` inputs (the
    // UGen reads them without a default). When the memory is full, or the
    // unit does not have two outputs or enough inputs, the pool is null: the
    // UGen must then call failed() and do no more.
    GrainUnit(int maxGrainsInput, int minInputs) : mMinInputs(minInputs) {
        // The class files before the stereo output made SynthDefs with one
        // output. The engine writes two.
        if (!usableShape())
            return;
        const float size = input(maxGrainsInput, kDefaultGrains);
        // NaN gives the default.
        const int poolSize = std::isnan(size) ? kDefaultGrains
                                              : static_cast<int>(std::lround(std::fmin(std::fmax(size, 1.f), kMaxGrains)));
        mPool = static_cast<Grain*>(RTAlloc(mWorld, poolSize * sizeof(Grain)));
        if (!mPool)
            return;
        mCore.init(mPool, poolSize, sampleRate(), bufferSize());
        // The random generator of the synth: RandSeed sets it.
        RGen& rgen = *mParent->mRGen;
        const uint32 a = rgen.trand();
        const uint32 b = rgen.trand();
        const uint32 c = rgen.trand();
        mCore.seed(a, b, c);
    }

    ~GrainUnit() {
        if (mPool)
            RTFree(mWorld, mPool);
    }

    // Makes the UGen silent for good, after a failed allocation, as
    // ClearUnitIfMemFailed of SuperCollider, or when the unit does not have
    // two outputs.
    void failed() {
        Unit* unit = this;
        if (!usableShape()) {
            Print("SuperPos: the SynthDef has a SuperPos UGen with %d outputs and %d inputs; it needs 2 outputs "
                  "and at least %d inputs. Make the SynthDef again with the current class file.\n",
                  static_cast<int>(numOutputs()), static_cast<int>(numInputs()), mMinInputs);
            SETCALC(ClearUnitOutputs);
            ClearUnitOutputs(unit, 1);
            unit->mDone = true;
            return;
        }
        ClearUnitOnMemFailed
    }

    // Two outputs and enough inputs.
    bool usableShape() const { return numOutputs() == 2 && static_cast<int>(numInputs()) >= mMinInputs; }

    float input(int index, float otherwise) const { return numInputs() > index ? in0(index) : otherwise; }

    // The engine controls, from input `first` on.
    Controls controls(int first) const {
        return { in0(first), in0(first + 1), in0(first + 2), input(first + 3, 0.f), input(first + 4, 1.f),
                 input(first + 5, 1.f), input(first + 6, 0.f), input(first + 7, 0.f), input(first + 8, 0.f),
                 input(first + 9, 0.f), input(first + 10, 1.f) };
    }

    // A buffer number that is not a number, or too large for an int.
    bool badBufferNumber() const { return !(std::fabs(in0(0)) < 2.0e9f); }

    int mMinInputs;
    Grain* mPool = nullptr;
    Engine mCore;
};

} // namespace superpos
