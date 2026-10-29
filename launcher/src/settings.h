#pragma once

#include <string>

namespace nla {

struct Settings {
    // The game folder: "naruto", "naruto2", "gnt3" or "gnt4". The project itself is always
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

    static Settings load();
    void save() const;

    static std::wstring file_path();
};

}  // namespace nla
