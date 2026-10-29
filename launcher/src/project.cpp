#include "project.h"

#include "util.h"
#include "version.h"

#include <regex>

namespace nla {
namespace {

constexpr wchar_t kFallbackSha1[] = L"19251f7f15f8b6f7e0cd1c753409151ec49d9af2";
constexpr wchar_t kFallbackDiscId[] = L"GNREDA";

bool looks_like_project(const fs::path& root) {
    std::error_code ec;
    return fs::is_regular_file(root / L"naruto" / L"CMakeLists.txt", ec) &&
           fs::is_regular_file(root / L"tools" / L"extract_disc.py", ec);
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

Project::Project(fs::path root) : root_(std::move(root)) {}

bool Project::valid() const { return looks_like_project(root_); }

fs::path Project::naruto() const { return root_ / L"naruto"; }
fs::path Project::disc() const { return naruto() / L"rom" / L"disc"; }
fs::path Project::dol() const { return disc() / L"sys" / L"main.dol"; }
fs::path Project::manifest() const { return disc() / L"disc.manifest"; }
fs::path Project::build_dir() const { return naruto() / L"build"; }
fs::path Project::binary() const {
    return build_dir() / (std::wstring(L"naruto") + exe_suffix());
}
fs::path Project::controls() const { return naruto() / L"controls.txt"; }
fs::path Project::saves() const { return naruto() / L"saves"; }
fs::path Project::extract_script() const { return root_ / L"tools" / L"extract_disc.py"; }
fs::path Project::cmake_lists() const { return naruto() / L"CMakeLists.txt"; }

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
    std::vector<fs::path> starts;
    if (!saved.empty()) {
        fs::path path(saved);
        if (looks_like_project(path)) return path;
    }

    std::error_code ec;
    fs::path app(exe_dir());
    starts.push_back(app);
    starts.push_back(fs::current_path(ec));

    for (const auto& start : starts) {
        for (fs::path candidate = start; !candidate.empty();) {
            if (looks_like_project(candidate)) return candidate;
            auto parent = candidate.parent_path();
            if (parent == candidate) break;
            candidate = parent;
        }
    }
    // Nothing on disk yet: a bundle that carries the sources supplies them.
    return seed_bundled_project();
}

}  // namespace nla
