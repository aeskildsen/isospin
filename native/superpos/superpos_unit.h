// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The parts that the SuperPos UGens share: the grain pool, the engine and
// its random seed, and the inputs of the engine controls.
//
// The engine controls are 11 inputs after the inputs of the source, in this
// order: grainDur, overlap, transpose, regularity, probability, texture,
// detuneStep, scatter, pan, stereoSpread, normalize. A SynthDef with fewer
// inputs (made with an older class file) gets the default value of each
// missing input after transpose.
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
    // The size of the grain pool.
    static constexpr int kMaxGrains = 64;

    // Gets the pool from the real-time memory and seeds the engine. When the
    // memory is full, the pool is null: the UGen must then call
    // failed() and do no more.
    GrainUnit() {
        mPool = static_cast<Grain*>(RTAlloc(mWorld, kMaxGrains * sizeof(Grain)));
        if (!mPool)
            return;
        mCore.init(mPool, kMaxGrains, sampleRate(), bufferSize());
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
    // ClearUnitIfMemFailed of SuperCollider.
    void failed() {
        Unit* unit = this;
        ClearUnitOnMemFailed
    }

    float input(int index, float otherwise) const { return numInputs() > index ? in0(index) : otherwise; }

    // The engine controls, from input `first` on.
    Controls controls(int first) const {
        return { in0(first), in0(first + 1), in0(first + 2), input(first + 3, 0.f), input(first + 4, 1.f),
                 input(first + 5, 1.f), input(first + 6, 0.f), input(first + 7, 0.f), input(first + 8, 0.f),
                 input(first + 9, 0.f), input(first + 10, 1.f) };
    }

    // A buffer number that is not a number, or too large for an int.
    bool badBufferNumber() const { return !(std::fabs(in0(0)) < 2.0e9f); }

    Grain* mPool = nullptr;
    Engine mCore;
};

} // namespace superpos
