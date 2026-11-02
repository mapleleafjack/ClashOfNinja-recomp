#include "picker.h"

#include <algorithm>

#include "app_shared.h"
#include "util.h"

namespace nla {

namespace {

// A disc patch is an xdelta of the whole disc: SCON4, a translation, a rebalancing hack.
bool is_patch_delta(const fs::path& path) {
    for (const wchar_t* extension : {L".xdelta", L".xdelta3", L".vcdiff"}) {
        if (ends_with_ci(path.wstring(), extension)) return true;
    }
    return false;
}

}  // namespace

void Picker::open(PickerKind kind, std::wstring title, const fs::path& start,
                  std::function<void(const fs::path&)> accept) {
    std::error_code ec;
    fs::path folder = start;
    if (!folder.empty() && !fs::is_directory(folder, ec)) folder = folder.parent_path();
    if (folder.empty() || !fs::is_directory(folder, ec)) folder = home_dir();
    kind_ = kind;
    title_ = std::move(title);
    accept_ = std::move(accept);
    folder_ = folder.wstring();
    path_ = folder_;
    editing_ = false;
    list();
}

void Picker::close() {
    kind_ = PickerKind::None;
    accept_ = nullptr;
    entries_.clear();
    editing_ = false;
    note_.clear();
}

void Picker::list() {
    entries_.clear();
    std::error_code ec;
    fs::path folder(folder_);
    const bool readable = fs::is_directory(folder, ec);
    if (readable && folder.has_parent_path() && folder.parent_path() != folder) {
        entries_.push_back({L"..", true});
    }
    std::vector<PickerEntry> directories, files;
    if (readable) {
        for (const auto& entry : fs::directory_iterator(
                 folder, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code item_ec;
            auto name = entry.path().filename().wstring();
            if (name.empty() || name[0] == L'.') continue;
            if (entry.is_directory(item_ec)) {
                directories.push_back({name, true});
            } else if (kind_ == PickerKind::Image && is_disc_image(entry.path())) {
                files.push_back({name, false});
            } else if (kind_ == PickerKind::Patch && is_patch_delta(entry.path())) {
                files.push_back({name, false});
            }
        }
    }
    auto by_name = [](const PickerEntry& left, const PickerEntry& right) {
        return to_lower(left.name) < to_lower(right.name);
    };
    std::sort(directories.begin(), directories.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    entries_.insert(entries_.end(), directories.begin(), directories.end());
    entries_.insert(entries_.end(), files.begin(), files.end());
    selected_ = -1;
    offset_ = 0;
    note_.clear();
}

}  // namespace nla
