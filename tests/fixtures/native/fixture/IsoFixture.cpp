// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The IsoFixture test UGen: audio in, multiplied by a gain. The build checks
// of isospin use it to test the path for hand-written UGens. It is not in a
// release.
#include <SC_PlugIn.hpp>

#include <isospin/sc_plugin_table.h>

#include "fixture_core.h"

namespace {

class IsoFixture : public SCUnit {
public:
    IsoFixture() {
        set_calc_function<IsoFixture, &IsoFixture::next>();
        next(1);
    }

private:
    // Inputs: 0 the signal (audio rate), 1 the gain (control rate).
    void next(int numSamples) {
        isospin_fixture::apply_gain(in(0), out(0), numSamples, in0(1));
    }
};

} // namespace

void IsoFixture_Load(InterfaceTable* inTable) {
    ISOSPIN_KEEP_TABLE(inTable);
    registerUnit<IsoFixture>(inTable, "IsoFixture");
}
