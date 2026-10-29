#pragma once

#include <optional>
#include <string>

#include <SDL.h>

#include "util.h"

namespace nla {

// Which of the platform's own choosers to show.
enum class DialogKind { Folder, Image };

// What a chooser answered. `shown` keeps "the user cancelled" apart from "this machine
// has no chooser to show", where the launcher falls back to its own picker.
struct DialogAnswer {
    bool shown = false;
    std::optional<fs::path> path;
};

// Shows the system chooser, modal over the launcher's window, starting at `start` — a
// folder, or a file whose folder and name are preselected.
DialogAnswer show_native_dialog(DialogKind kind, SDL_Window* parent, const std::wstring& title,
                                const fs::path& start);

}  // namespace nla
