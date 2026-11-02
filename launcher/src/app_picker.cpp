// Choosing a folder or a disc: the platform's own chooser, and the launcher's own
// picker for the systems that have none.

#include "app.h"
#include "app_shared.h"

#include "dialog.h"

#include "util.h"

#include <algorithm>

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

void App::choose_image() {
    if (!project_) return;
    fs::path start =
        settings_.last_image.empty() ? project_->root() : fs::path(settings_.last_image);
    choose_path(PickerKind::Image, L"Choose your own disc image", start,
                [this](const fs::path& chosen) { start_setup(chosen); });
}

// -- choosing a folder or an image -----------------------------------------

void App::choose_path(PickerKind kind, const std::wstring& title, const fs::path& start,
                      std::function<void(const fs::path&)> accept) {
    if (!headless_) {
        const DialogKind kind_of = kind == PickerKind::Folder ? DialogKind::Folder
                                   : kind == PickerKind::Patch ? DialogKind::Patch
                                                               : DialogKind::Image;
        auto answer = show_native_dialog(kind_of, window_.handle(), title, start);
        if (answer.shown) {
            if (answer.path && accept) accept(*answer.path);
            invalidate();
            return;
        }
        // The system has a chooser but would not open it. Say so, because the picker that
        // follows is not what was asked for and would otherwise look like a regression.
        append_log(L"[launcher] the system chooser did not open; using the launcher's own\n");
    }
    open_picker(kind, title, start, std::move(accept));
}

void App::open_picker(PickerKind kind, const std::wstring& title, const fs::path& start,
                      std::function<void(const fs::path&)> accept) {
    std::error_code ec;
    fs::path folder = start;
    if (!folder.empty() && !fs::is_directory(folder, ec)) folder = folder.parent_path();
    if (folder.empty() || !fs::is_directory(folder, ec)) folder = home_dir();
    picker_ = kind;
    picker_title_ = title;
    picker_accept_ = std::move(accept);
    picker_folder_ = folder.wstring();
    picker_path_ = picker_folder_;
    picker_editing_ = false;
    list_picker();
    settings_open_ = false;
    overlay_.close();
    scale_open_ = false;
    window_.set_text_input(true);
    invalidate();
}

void App::close_picker() {
    picker_ = PickerKind::None;
    picker_accept_ = nullptr;
    picker_entries_.clear();
    picker_editing_ = false;
    picker_note_.clear();
    window_.set_text_input(false);
    invalidate();
}

void App::list_picker() {
    picker_entries_.clear();
    std::error_code ec;
    fs::path folder(picker_folder_);
    bool readable = fs::is_directory(folder, ec);
    if (readable && folder.has_parent_path() && folder.parent_path() != folder) {
        picker_entries_.push_back({L"..", true});
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
            } else if (picker_ == PickerKind::Image && is_disc_image(entry.path())) {
                files.push_back({name, false});
            } else if (picker_ == PickerKind::Patch && is_patch_delta(entry.path())) {
                files.push_back({name, false});
            }
        }
    }
    auto by_name = [](const PickerEntry& left, const PickerEntry& right) {
        return to_lower(left.name) < to_lower(right.name);
    };
    std::sort(directories.begin(), directories.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    picker_entries_.insert(picker_entries_.end(), directories.begin(), directories.end());
    picker_entries_.insert(picker_entries_.end(), files.begin(), files.end());
    picker_selected_ = -1;
    picker_offset_ = 0;
    picker_note_.clear();
}

void App::accept_picker() {
    if (picker_ == PickerKind::None) return;
    std::error_code ec;
    fs::path chosen;
    bool listed = picker_selected_ >= 0 &&
                  picker_selected_ < static_cast<int>(picker_entries_.size()) &&
                  !picker_entries_[static_cast<size_t>(picker_selected_)].directory;

    if (listed) {
        chosen = fs::path(picker_folder_) /
                 picker_entries_[static_cast<size_t>(picker_selected_)].name;
    } else if (!picker_path_.empty()) {
        chosen = fs::path(picker_path_);
        // A folder is somewhere to look in the image picker, and the answer in the
        // folder picker — descending there is what made "Use This Folder" do nothing.
        if (picker_ == PickerKind::Image && fs::is_directory(chosen, ec)) {
            picker_folder_ = chosen.wstring();
            picker_path_ = picker_folder_;
            picker_editing_ = false;
            list_picker();
            invalidate();
            return;
        }
    }
    if (chosen.empty()) {
        picker_note_ = L"Type a path, or choose one from the list.";
        invalidate();
        return;
    }
    if (picker_ == PickerKind::Folder) {
        if (!fs::is_directory(chosen, ec)) {
            picker_note_ = L"That folder does not exist.";
            invalidate();
            return;
        }
    } else if (!fs::is_regular_file(chosen, ec)) {
        picker_note_ = L"That file does not exist.";
        invalidate();
        return;
    }

    auto accept = picker_accept_;
    close_picker();
    if (accept) accept(chosen);
}

}  // namespace nla
