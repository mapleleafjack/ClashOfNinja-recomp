#include "patches.h"

#include "tools.h"

#include <algorithm>
#include <vector>

namespace nla {
namespace {

// The fields of a JSON answer, read without a parser: GitHub's release answer is a flat
// enough list for the few keys the launcher needs, and this cannot be made to allocate by
// a malformed answer.
std::wstring json_string(const std::wstring& json, const std::wstring& key, size_t from,
                         size_t* after) {
    const std::wstring needle = L"\"" + key + L"\"";
    size_t at = json.find(needle, from);
    if (at == std::wstring::npos) return {};
    at = json.find(L':', at + needle.size());
    if (at == std::wstring::npos) return {};
    ++at;
    while (at < json.size() && (json[at] == L' ' || json[at] == L'\t' || json[at] == L'\n' ||
                                json[at] == L'\r')) {
        ++at;
    }
    if (at >= json.size() || json[at] != L'"') return {};
    std::wstring value;
    for (size_t index = at + 1; index < json.size(); ++index) {
        const wchar_t character = json[index];
        if (character == L'\\' && index + 1 < json.size()) {
            value.push_back(json[++index]);
            continue;
        }
        if (character == L'"') {
            if (after) *after = index + 1;
            return value;
        }
        value.push_back(character);
    }
    return {};
}

std::vector<std::wstring> json_strings(const std::wstring& json, const std::wstring& key) {
    std::vector<std::wstring> values;
    size_t from = 0;
    for (;;) {
        size_t after = 0;
        auto value = json_string(json, key, from, &after);
        if (after == 0) break;
        values.push_back(value);
        from = after;
    }
    return values;
}

std::wstring file_name_of(const std::wstring& url) {
    const auto slash = url.find_last_of(L'/');
    return slash == std::wstring::npos ? url : url.substr(slash + 1);
}

std::wstring api_url(const PatchSource& source) {
    return L"https://api.github.com/repos/" + source.owner + L"/" + source.repo +
           L"/releases/latest";
}

}  // namespace

PatchRelease find_patch_release(const PatchSource& source) {
    PatchRelease release;
    const auto curl = curl_tool();
    if (curl.empty()) {
        release.error = L"curl was not found, and the launcher uses it to reach GitHub";
        return release;
    }
    std::string body;
    int status = -1;
    const bool ok =
        capture_output(curl,
                       {L"-fsSL", L"--max-time", L"15", L"--connect-timeout", L"6", L"-H",
                        L"Accept: application/vnd.github+json", L"-A", L"NarutoLauncher",
                        api_url(source)},
                       &body, &status) &&
        status == 0;
    if (!ok) {
        release.error = L"GitHub did not answer; there may be no network connection";
        return release;
    }
    const std::wstring json = widen(body);
    auto tag = json_string(json, L"tag_name", 0, nullptr);
    if (tag.empty()) {
        release.error = L"the release could not be read from GitHub's answer";
        return release;
    }
    release.version = tag.front() == L'v' || tag.front() == L'V' ? tag.substr(1) : tag;
    release.page_url = json_string(json, L"html_url", 0, nullptr);
    // The asset by name: a release may carry more than the patch, and the name is the only
    // thing that says which one the delta is inside.
    for (const auto& url : json_strings(json, L"browser_download_url")) {
        if (file_name_of(url) != source.asset) continue;
        release.asset_url = url;
        release.asset_name = source.asset;
        break;
    }
    if (release.asset_url.empty()) {
        release.error = L"the newest release does not carry " + source.asset;
    }
    return release;
}

fs::path patch_directory(const fs::path& project_root) { return project_root / L"patches"; }

}  // namespace nla
