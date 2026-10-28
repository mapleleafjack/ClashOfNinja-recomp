#pragma once

#include <string>

namespace nla {

struct Settings {
    std::wstring project_root;
    std::wstring last_image;
    int scale = 2;
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
