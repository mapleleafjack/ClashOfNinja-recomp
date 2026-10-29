#include "project.h"

#include "project_templates.h"
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
    for (const auto& game : kGames) {
        std::wstring controls = game.folder + L"/controls.txt";
        if (relative == controls) return true;
        for (const wchar_t* name : {L"rom", L"build", L"saves"}) {
            std::wstring dir = game.folder + L"/" + name;
            std::wstring_view prefix(dir);
            if (relative == prefix) return true;
            if (relative.size() > prefix.size() &&
                relative.compare(0, prefix.size(), prefix) == 0 &&
                relative[prefix.size()] == L'/') {
                return true;
            }
        }
    }
    return false;
}

// A bundle may carry a project in Contents/Resources/project, and a signed bundle must not
// be written into, so it is copied where a build can write: beside the launcher's
// settings. The marker holds the launcher's version, so an updated launcher lays its copy
// down once without disturbing the disc, the
// build or the saves.
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
fs::path Project::analysis_symbols() const {
    return game_dir() / L"analysis" / L"symbols.txt";
}

bool Project::create(const fs::path& root, std::wstring* error) {
    auto fail = [error](const std::wstring& text) {
        if (error) *error = text;
        return false;
    };

    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return fail(L"Could not create " + root.wstring() + L": " + widen(ec.message()));

    for (const auto& game : game_templates()) {
        fs::path dir = root / game.folder;
        // A folder that has one of the game folders already is someone's project: never
        // write into it over their files.
        if (fs::exists(dir, ec) && !fs::is_regular_file(dir / L"CMakeLists.txt", ec)) {
            return fail(dir.wstring() + L" is already there and is not a game folder. Choose "
                        L"an empty folder, or one the launcher set up before.");
        }
        fs::create_directories(dir / L"analysis", ec);
        fs::create_directories(dir / L"recomp", ec);
        if (ec) return fail(L"Could not create " + dir.wstring() + L": " + widen(ec.message()));
        for (const auto& file : game.files) {
            fs::path path = dir / file.path;
            if (fs::exists(path, ec)) continue;
            if (!write_text_file(path, file.text))
                return fail(L"Could not write " + path.wstring());
        }
    }

    // The recompiler all four projects add, and the script the launcher looks for as the
    // project's marker, both travel with the launcher: beside it on Windows and Linux, in
    // Contents/Resources inside a macOS bundle. A development build finds them above
    // itself, in the checkout it was built from.
    fs::path shipped = bundle_resources_dir();
    if (shipped.empty()) {
        for (fs::path candidate = fs::path(exe_dir()); !candidate.empty();
             candidate = candidate.parent_path()) {
            if (fs::is_regular_file(candidate / L"gcn-recomp" / L"CMakeLists.txt", ec) &&
                fs::is_regular_file(candidate / L"tools" / L"extract_disc.py", ec)) {
                shipped = candidate;
                break;
            }
            if (candidate.parent_path() == candidate) break;
        }
    }
    if (shipped.empty()) {
        return fail(L"No gcn-recomp folder and tools\\extract_disc.py were found beside the "
                    L"launcher, so the game cannot be built. Unzip the whole package, not just "
                    L"the executable.");
    }
    fs::path recomp = shipped / L"gcn-recomp";
    if (!fs::exists(root / L"gcn-recomp", ec)) {
        fs::copy(recomp, root / L"gcn-recomp", fs::copy_options::recursive, ec);
        if (ec) return fail(L"Could not copy the recompiler: " + widen(ec.message()));
    }

    fs::path tools = shipped / L"tools";
    if (!fs::is_regular_file(tools / L"extract_disc.py", ec)) {
        return fail(L"The launcher has no tools\\extract_disc.py beside it. Unzip the whole "
                    L"package, not just the executable.");
    }
    fs::create_directories(root / L"tools", ec);
    for (const wchar_t* name : {L"extract_disc.py", L"setup.sh"}) {
        if (!fs::is_regular_file(tools / name, ec)) continue;
        if (fs::exists(root / L"tools" / name, ec)) continue;
        fs::copy_file(tools / name, root / L"tools" / name, fs::copy_options::none, ec);
        if (ec) return fail(L"Could not copy " + (root / L"tools" / name).wstring());
    }

    if (!looks_like_project(root)) return fail(L"Could not set up the game folders in " +
                                               root.wstring());
    return true;
}

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

std::optional<fs::path> Project::locate() {
    // The launcher works in its own folder, always: the game folders it writes, the disc it
    // extracts, the build and the saves all sit beside the executable. Nothing above it, no
    // folder it was started from and no remembered path is ever used instead, so a copy
    // carried somewhere else takes its games with it.
    std::error_code ec;
    // A signed macOS bundle must not be written into, so that release builds in the copy of
    // the project it laid down in the user's own folder.
    if (auto seeded = seed_bundled_project()) return seeded;
    if (!bundle_resources_dir().empty()) {
        fs::path project = user_data_dir() / L"project";
        fs::create_directories(project, ec);
        if (ec) return std::nullopt;
        return project;
    }
    return fs::path(exe_dir());
}

}  // namespace nla
