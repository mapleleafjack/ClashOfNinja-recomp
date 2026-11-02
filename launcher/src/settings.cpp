#include "settings.h"

#include "util.h"

#include <map>

namespace nla {
namespace {

// The separator between the code names saved for one game. A code's name is whatever its
// author called it and may hold a comma, so a comma would split a name in two; the unit
// separator is not one an editor writes into a name.
constexpr wchar_t kCodeSeparator = L'\x1F';

// The settings file is a flat INI: one [launcher] section of key=value lines.
std::map<wstring, wstring> read(const fs::path& path) {
    std::map<wstring, wstring> values;
    auto text = read_text_file(path);
    for (const auto& line : split(text, L'\n')) {
        auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == L'[' || trimmed.front() == L';') continue;
        auto equals = trimmed.find(L'=');
        if (equals == wstring::npos) continue;
        values[trim(trimmed.substr(0, equals))] = trim(trimmed.substr(equals + 1));
    }
    return values;
}

int read_int(const std::map<wstring, wstring>& values, const wchar_t* key, int fallback) {
    auto found = values.find(key);
    if (found == values.end() || found->second.empty()) return fallback;
    try {
        return std::stoi(found->second);
    } catch (...) {
        return fallback;
    }
}

bool read_bool(const std::map<wstring, wstring>& values, const wchar_t* key, bool fallback) {
    auto found = values.find(key);
    if (found == values.end() || found->second.empty()) return fallback;
    return found->second == L"1" || to_lower(found->second) == L"true";
}

wstring read_text(const std::map<wstring, wstring>& values, const wchar_t* key) {
    auto found = values.find(key);
    return found == values.end() ? wstring() : found->second;
}

}  // namespace

std::wstring Settings::file_path() {
    return (user_data_dir() / L"settings.ini").wstring();
}

Settings Settings::load() {
    auto values = read(fs::path(file_path()));
    Settings settings;
    settings.game = read_text(values, L"game");
    settings.last_image = read_text(values, L"lastImage");
    settings.scale = read_int(values, L"scale", 2);
    if (settings.scale < 1 || settings.scale > 4) settings.scale = 2;
    settings.players = read_int(values, L"players", 4);
    if (settings.players < 1 || settings.players > 4) settings.players = 4;
    settings.mute_music = read_bool(values, L"muteMusic", false);
    settings.input_log = read_bool(values, L"inputLog", true);
    settings.check_updates = read_bool(values, L"checkUpdates", true);
    settings.skipped_update = read_text(values, L"skippedUpdate");
    settings.window_width = read_int(values, L"windowWidth", 1060);
    settings.window_height = read_int(values, L"windowHeight", 820);
    if (settings.window_width < 940) settings.window_width = 940;
    if (settings.window_height < 700) settings.window_height = 700;
    // `mods.<game> = <mod>,<mod>`: one key per game, so the list a game has does not
    // depend on the order the launcher wrote them in.
    for (const auto& [key, value] : values) {
        if (key.rfind(L"mods.", 0) != 0 || key.size() <= 5) continue;
        std::vector<wstring> folder_names;
        for (const auto& part : split(value, L',')) {
            auto name = trim(part);
            if (!name.empty()) folder_names.push_back(name);
        }
        settings.mods[key.substr(5)] = std::move(folder_names);
    }
    // `patch.<game> = <delta path>`: the disc patch that game is played with.
    for (const auto& [key, value] : values) {
        if (key.rfind(L"patch.", 0) != 0 || key.size() <= 6) continue;
        if (!trim(value).empty()) settings.patches[key.substr(6)] = trim(value);
    }
    // `codes.<game> = <code>` per name, separated by the unit separator: a code's name is
    // whatever its author called it, and commas are common enough in them that a comma
    // would split a name in two.
    for (const auto& [key, value] : values) {
        if (key.rfind(L"codes.", 0) != 0 || key.size() <= 6) continue;
        std::vector<wstring> names;
        for (const auto& part : split(value, kCodeSeparator)) {
            auto name = trim(part);
            if (!name.empty()) names.push_back(name);
        }
        settings.codes[key.substr(6)] = std::move(names);
    }
    return settings;
}

void Settings::save() const {
    std::error_code ec;
    fs::create_directories(user_data_dir(), ec);

    wstring text = L"; Naruto: Clash of Ninja launcher settings\n[launcher]\n";
    auto put = [&text](const wchar_t* key, const wstring& value) {
        text += format(L"%ls=%ls\n", key, value.c_str());
    };
    put(L"game", game);
    put(L"lastImage", last_image);
    put(L"scale", std::to_wstring(scale));
    put(L"players", std::to_wstring(players));
    put(L"muteMusic", mute_music ? L"1" : L"0");
    put(L"inputLog", input_log ? L"1" : L"0");
    put(L"checkUpdates", check_updates ? L"1" : L"0");
    put(L"skippedUpdate", skipped_update);
    put(L"windowWidth", std::to_wstring(window_width));
    put(L"windowHeight", std::to_wstring(window_height));
    for (const auto& [game, folder_names] : mods) {
        put((L"mods." + game).c_str(), join(folder_names, L","));
    }
    for (const auto& [game, delta] : patches) {
        put((L"patch." + game).c_str(), delta);
    }
    for (const auto& [game, names] : codes) {
        put((L"codes." + game).c_str(), join(names, wstring(1, kCodeSeparator)));
    }
    write_text_file(fs::path(file_path()), narrow(text));
}

}  // namespace nla
