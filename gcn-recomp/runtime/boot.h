// Boot: the checks made before the game's first instruction runs.
#pragma once

#include <cstdint>
#include <cstring>

// What the disc check found.
enum class DiscCheck {
    Matched,     // the header is the disc this build was recompiled from
    WrongDisc,   // no patch: the game is reading another disc
    WrongTree,   // a patch, and the tree it decodes from is another disc
};

// Which header says whether the game is the disc this build was recompiled from. A patch is
// a whole disc of its own, and a hack may rename the ID it carries -- SCON4 is SG4JDA over a
// G4NJDA disc -- so while one is on, the disc this build was made for is the tree the patch
// decodes from, and the patched disc is meant to differ. `tree_header` is that tree's
// header, or null when there is none: an image rather than a tree, or no patch at all.
inline DiscCheck boot_check_disc(const uint8_t* disc_header, const uint8_t* tree_header,
                                 const char* expected) {
    const uint8_t* checked = tree_header ? tree_header : disc_header;
    if (std::memcmp(checked, expected, 6) == 0) return DiscCheck::Matched;
    return tree_header ? DiscCheck::WrongTree : DiscCheck::WrongDisc;
}
