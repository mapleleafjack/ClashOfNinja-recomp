// The mods' Gecko codes, run once a frame.
//
// The launcher writes `mods.gecko` beside `disc.manifest`: the operations a set of Gecko
// codes became (see launcher/src/gecko.cpp for the grammar), already resolved from the
// community's codetypes. This reads that file and runs it on the guest thread, once for
// each frame the Video Interface retraces, which is when a console's code handler runs
// them. Writes go into guest memory, so a code that changes a table or a flag works; a
// code that patches an instruction cannot, because recompilation already turned those
// instructions into this machine's own -- those words are applied when the game is
// recompiled instead (mods.code), never here.
#pragma once

#include <cstdint>

// Reads `mods.gecko` from the extracted disc's folder. Called once, before the game runs.
void gecko_init(const char* disc_dir);

// Runs the program once: the frame boundary, on the guest thread.
void gecko_frame();
