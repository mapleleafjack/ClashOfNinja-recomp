#pragma once

#include <array>
#include <vector>

namespace nla {

// One file of a game project's own code: its path under <project>/<game>/, and its text.
// The parts that come off the disc -- rom/disc and analysis/symbols.txt -- are not here.
struct TemplateFile {
    const wchar_t* path;
    const char* text;
};

struct GameTemplate {
    const wchar_t* folder;
    std::array<TemplateFile, 6> files;
};

// The four game projects, as the launcher writes them into its own folder.
const std::vector<GameTemplate>& game_templates();

}  // namespace nla
