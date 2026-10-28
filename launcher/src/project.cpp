#include "project.h"

#include "util.h"

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
    return std::nullopt;
}

}  // namespace nla
