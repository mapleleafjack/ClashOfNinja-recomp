#include "project.h"

#include "util.h"
#include "version.h"

#include <regex>

namespace nla {
namespace {

constexpr wchar_t kFallbackSha1[] = L"19251f7f15f8b6f7e0cd1c753409151ec49d9af2";
constexpr wchar_t kFallbackDiscId[] = L"GNREDA";

const std::vector<GameInfo> kGames = {
    {L"naruto", L"Naruto: Clash of Ninja", L"Clash of Ninja"},
    {L"naruto2", L"Naruto: Clash of Ninja 2", L"Clash of Ninja 2"},
    {L"gnt3", L"Naruto: Gekitou Ninja Taisen! 3", L"Ninja Taisen 3"},
    {L"gnt4", L"Naruto: Gekitou Ninja Taisen! 4", L"Ninja Taisen 4"},
};

bool has_game(const fs::path& root, const std::wstring& folder) {
    std::error_code ec;
    return fs::is_regular_file(root / folder / L"CMakeLists.txt", ec);
}

// The game the project supplies, preferring the one asked for.
std::wstring pick_game(const fs::path& root, const std::wstring& wanted) {
    if (!wanted.empty() && has_game(root, wanted)) return wanted;
    for (const auto& game : kGames) {
        if (has_game(root, game.folder)) return game.folder;
    }
    return wanted.empty() ? std::wstring(kGames.front().folder) : wanted;
}

bool looks_like_project(const fs::path& root) {
    std::error_code ec;
    if (!fs::is_regular_file(root / L"tools" / L"extract_disc.py", ec)) return false;
    for (const auto& game : kGames) {
        if (has_game(root, game.folder)) return true;
    }
    return false;
}

// What the disc supplied, what the build writes, and what the player changed: a newer
// launcher's copy of the sources must leave all of it alone.
bool is_runtime_path(std::wstring_view relative) {
    if (relative == L"naruto/controls.txt") return true;
    for (const wchar_t* name : {L"naruto/rom", L"naruto/build", L"naruto/saves"}) {
        std::wstring_view prefix(name);
        if (relative == prefix) return true;
        if (relative.size() > prefix.size() && relative.compare(0, prefix.size(), prefix) == 0 &&
            relative[prefix.size()] == L'/') {
            return true;
        }
    }
    return false;
}

// A macOS release carries the game's sources in Contents/Resources/project, and a signed
// bundle must not be written into, so they are copied where a build can write: beside the
// launcher's settings. The marker holds the launcher's version, so an updated launcher
// lays down its own sources once without disturbing the disc, the build or the saves.
std::optional<fs::path> seed_bundled_project() {
    fs::path resources = bundle_resources_dir();
    if (resources.empty()) return std::nullopt;
    fs::path source = resources / L"project";
    if (!looks_like_project(source)) return std::nullopt;

    fs::path data = user_data_dir();
    if (data.empty()) return std::nullopt;
    fs::path destination = data / L"project";
    fs::path marker = data / L"project.version";

    if (read_text_file(marker) != launcher_version() ||
        !looks_like_project(destination)) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(
                 source, fs::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end; it.increment(ec)) {
            auto relative = fs::relative(it->path(), source, ec);
            if (ec) break;
            if (is_runtime_path(relative.generic_wstring())) {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            fs::path target = destination / relative;
            fs::create_directories(target.parent_path(), ec);
            if (ec) break;
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec);
            if (ec) break;
        }
        if (ec) return std::nullopt;
        write_text_file(marker, narrow(launcher_version()));
    }
    if (looks_like_project(destination)) return destination;
    return std::nullopt;
}

}  // namespace

const std::vector<GameInfo>& Project::games() { return kGames; }

std::wstring Project::default_game() { return kGames.front().folder; }

const GameInfo& Project::game_info(const std::wstring& folder) {
    for (const auto& game : kGames) {
        if (game.folder == folder) return game;
    }
    return kGames.front();
}

Project::Project(fs::path root, std::wstring game)
    : root_(std::move(root)), game_(pick_game(root_, game)) {}

bool Project::valid() const {
    std::error_code ec;
    return fs::is_regular_file(extract_script(), ec) && has_game(root_, game_);
}

std::wstring Project::title() const {
    auto text = read_text_file(cmake_lists());
    std::wregex pattern(L"TITLE\\s+\"([^\"]*)\"");
    std::wsmatch match;
    if (std::regex_search(text, match, pattern) && match.size() > 1)
        return match[1].str();
    return game_info(game_).title;
}

fs::path Project::game_dir() const { return root_ / game_; }
fs::path Project::covers_dir() const { return root_ / L"covers"; }
fs::path Project::disc() const { return game_dir() / L"rom" / L"disc"; }
fs::path Project::dol() const { return disc() / L"sys" / L"main.dol"; }
fs::path Project::manifest() const { return disc() / L"disc.manifest"; }
fs::path Project::build_dir() const { return game_dir() / L"build"; }
fs::path Project::binary() const {
    return build_dir() / (game_ + exe_suffix());
}
fs::path Project::controls() const { return game_dir() / L"controls.txt"; }
fs::path Project::saves() const { return game_dir() / L"saves"; }
fs::path Project::extract_script() const { return root_ / L"tools" / L"extract_disc.py"; }
fs::path Project::cmake_lists() const { return game_dir() / L"CMakeLists.txt"; }

std::wstring Project::disc_id() const {
    auto text = read_text_file(cmake_lists());
    std::wregex pattern(L"DISC_ID\\s+([A-Za-z0-9]{4,8})");
    std::wsmatch match;
    if (std::regex_search(text, match, pattern) && match.size() > 1)
        return match[1].str();
    return kFallbackDiscId;
}

std::wstring Project::expected_sha1() const {
    auto text = read_text_file(cmake_lists());
    std::wregex pattern(L"DOL_SHA1\\s+([0-9a-fA-F]{40})");
    std::wsmatch match;
    if (std::regex_search(text, match, pattern) && match.size() > 1)
        return to_lower(match[1].str());
    return kFallbackSha1;
}

DiscStatus Project::disc_status() const {
    DiscStatus status;
    status.expected = expected_sha1();
    std::error_code ec;
    status.extracted = fs::is_regular_file(dol(), ec);
    if (!status.extracted) return status;
    status.sha1 = sha1_file(dol());
    status.verified = !status.sha1.empty() && status.sha1 == status.expected;
    return status;
}

ProjectStatus Project::status() const {
    ProjectStatus status;
    status.disc = disc_status();
    std::error_code ec;
    status.built = fs::is_regular_file(binary(), ec);
    return status;
}

std::optional<fs::path> Project::locate(const std::wstring& saved) {
    // A release ships the game's sources and was built to use them, so the folder beside
    // or above the launcher wins: a folder remembered from another copy used to come first,
    // which made a fresh release build a checkout elsewhere and look as if it had brought
    // nothing with it. A remembered folder still counts when the launcher sits nowhere near
    // a project.
    std::error_code ec;
    fs::path app(exe_dir());
    if (looks_like_project(app)) return app;
    // The macOS bundle keeps its copy in Resources and builds from the one it laid down.
    if (auto shipped = seed_bundled_project()) return shipped;

    std::error_code walk_ec;
    std::vector<fs::path> starts;
    starts.push_back(app);
    starts.push_back(fs::current_path(walk_ec));

    for (const auto& start : starts) {
        for (fs::path candidate = start; !candidate.empty();) {
            if (looks_like_project(candidate)) return candidate;
            auto parent = candidate.parent_path();
            if (parent == candidate) break;
            candidate = parent;
        }
    }

    if (!saved.empty()) {
        fs::path path(saved);
        if (looks_like_project(path)) return path;
    }
    return std::nullopt;
}

}  // namespace nla
