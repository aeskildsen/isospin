// Copyright (C) 2026 Anders Eskildsen
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The interface table for a hand-written static UGen of isospin.
//
// Include this file once in the UGen wrapper source, after the SuperCollider
// plugin headers. It declares `ft`, the pointer through which the plugin
// macros (RTAlloc, Print and others) call the engine.
//
// Two builds use the same source:
//   - SuperSonic (CLOCKWORK_GUEST defined): one process can have more than one
//     engine (World). Each World has its own copy of the interface table, and
//     that copy goes away with its World. Thus `ft` points to the one constant
//     table (gInterfaceTable), and the plugin does not keep the table that
//     the load function gets.
//   - Stock scsynth headers: `ft` is the table that the load function gets,
//     as in a normal plugin.
//
// In the load function, call ISOSPIN_KEEP_TABLE(inTable) first. Then register
// each unit through inTable, for example registerUnit<MyUnit>(inTable, "Name").
// Do not keep inTable or the World in a static variable: the code must have no
// global state that changes, because more than one World can load it.
//
// Do not give a namespace-scope or static variable a value that needs code at
// run time (for example `const double pi = std::acos(-1.)`): SuperSonic's web
// build does not run dynamic initialisation, and the variable stays 0. Use
// constexpr values. scripts/check-native.py stops when a hand-written source
// needs dynamic initialisation.
//
// The load function has C++ linkage and the name that supercollider.toml gives
// in `load`. Do not use PluginLoad(): with stock headers and no STATIC_PLUGINS
// it makes the entry point of a loadable plugin, not <Name>_Load.
#pragma once

#ifdef CLOCKWORK_GUEST
static const InterfaceTable* const ft = &gInterfaceTable;
#    define ISOSPIN_KEEP_TABLE(table) ((void)(table))
#else
static InterfaceTable* ft;
#    define ISOSPIN_KEEP_TABLE(table) (ft = (table))
#endif
