// The IPL ROM font, built.
//
// Some of what a game says to the player is not the game's own text: it is the SDK's
// system font, read straight out of the console's IPL ROM at 0x1FCF00. Here that is the
// memory card prompts -- "Do you wish to proceed with saving?" and its Yes/No, and the
// description line under a menu. A runtime with no IPL ROM to read answers those with
// zeros, so the text is drawn as nothing: the dialog dims the screen and waits with no
// words on it.
//
// The ROM holds the Windows-1252 font as a Yay0-compressed blob, which the SDK decompresses
// itself; the format is documented in Dolphin's docs/gc-font-tool.cpp. This builds one from
// the public-domain 8x8 bitmap font below, so nothing of Nintendo's IPL is needed, and the
// geometry (24x24 cells, 21 to a row, a 512x512 2bpp sheet) is the one the bundled fonts
// use, which is what the SDK's layout arithmetic expects.
#pragma once
#include <cstdint>
#include <vector>

// The Windows-1252 font the IPL holds at ROM 0x1FCF00, Yay0-compressed, or empty if it
// could not be built.
const std::vector<uint8_t>& ipl_font_western();
