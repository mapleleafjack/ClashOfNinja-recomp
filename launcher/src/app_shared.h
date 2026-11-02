#pragma once

#include <algorithm>

#include "util.h"

namespace nla {

// The window the layout is drawn for. A bigger window zooms the whole interface rather
// than leaving the text at this size in the corner of a large screen.
constexpr float kDesignWidth = 1060.0f;
constexpr float kDesignHeight = 820.0f;
// Enough to make the text comfortable on a 1440p or larger display, and no more.
constexpr float kMaxZoom = 2.0f;

// A window bigger than the one the layout was drawn for zooms the whole interface up, so
// text and buttons keep their size against everything else on the page rather than sitting
// tiny in the corner of a large screen. Never below 1: a small window keeps the layout as
// drawn, down to the minimum size the window allows.
inline float ui_zoom_for(int pixel_width, int pixel_height, float dpi) {
    if (dpi <= 0.01f) return 1.0f;
    float zoom = std::min(static_cast<float>(pixel_width) / dpi / kDesignWidth,
                          static_cast<float>(pixel_height) / dpi / kDesignHeight);
    return std::clamp(zoom, 1.0f, kMaxZoom);
}

// True for a file the launcher reads as a disc image, or converts into one first.
inline bool is_disc_image(const fs::path& path) {
    for (const wchar_t* extension :
         {L".iso", L".ciso", L".rvz", L".wia", L".gcz", L".wbfs", L".tgc", L".nfs"}) {
        if (ends_with_ci(path.wstring(), extension)) return true;
    }
    return false;
}

}  // namespace nla
