#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace nla {

// What a set of Gecko codes becomes: a program the runtime runs once a frame, the
// instruction words recompilation applies, and what could not be read.
//
// A Gecko write into code cannot work at run time: the recompiler turned those
// instructions into this machine's own before the game started, so a write to their
// address changes nothing that runs. The words such a write carries are therefore also
// handed to the recompiler (mods.code), which keeps the ones that land in the DOL's text
// and translates those instructions instead of the disc's. The runtime's program still
// writes them into guest memory, which is the state the console would have had.
struct GeckoProgram {
    // One operation per line, in the grammar gecko.cpp documents; the runtime executes
    // this file (mods.gecko) as it is.
    std::string ops;
    // Address, word: 32-bit writes no condition guards, so recompilation can always
    // apply them. Writes the game only makes to data are dropped by the recompiler,
    // which is the only side that knows which addresses are code.
    std::vector<std::pair<uint32_t, uint32_t>> words;
    std::vector<std::wstring> warnings;
    std::size_t writes = 0;      // write operations, for the Activity log
    std::size_t conditions = 0;  // if/else/endif operations
};

// Reads codes as Gecko editors write them: `XXXXXXXX YYYYYYYY` pairs, `#` and `*`
// comment lines, `$` code names and `$$$$$$$$` between codes. `source` names the file in
// warnings, so a player can find the code that was skipped.
GeckoProgram parse_gecko(const std::wstring& text, const std::wstring& source);

}  // namespace nla
