#pragma once

#include <map>
#include <string>
#include <vector>

namespace nla {

struct Settings {
    // The game folder: "naruto", "naruto2", "gnt3", "gnt4" or "narutoeu". The project itself is always
    // the launcher's own folder, so there is no path to remember here.
    std::wstring game;
    std::wstring last_image;
    int scale = 2;
    // How many of the console's four pads a game controller may take, which the player
    // picker raises to the highest pad it names.
    int players = 4;
    bool mute_music = false;
    bool input_log = true;
    int window_width = 1060;
    int window_height = 820;
    bool check_updates = true;
    // A version the player said "not now" to, so the prompt does not return every start.
    std::wstring skipped_update;
    // The mods enabled for each game folder, by the mod's folder under mods/. A game only
    // offers the mods whose manifest names its disc ID, so this cannot carry one across
    // builds: a GNT4 mod never runs on another game's addresses.
    std::map<std::wstring, std::vector<std::wstring>> mods;
    // The disc patch each game folder is played with, by the delta's path. Empty for none:
    // a patch replaces the whole disc, so one is chosen rather than accumulated.
    std::map<std::wstring, std::wstring> patches;

    static Settings load();
    void save() const;

    static std::wstring file_path();
};

}  // namespace nla
