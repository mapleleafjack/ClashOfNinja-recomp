// Choosing a folder or a disc: the platform's own chooser, and the launcher's own
// picker for the systems that have none.

#include "app.h"
#include "app_shared.h"

#include "dialog.h"

#include "util.h"

#include <algorithm>

namespace nla {

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
    chooser_.open(kind, title, start, std::move(accept));
    settings_open_ = false;
    overlay_.close();
    scale_open_ = false;
    window_.set_text_input(true);
    invalidate();
}

void App::close_picker() {
    chooser_.close();
    window_.set_text_input(false);
    invalidate();
}

void App::list_picker() { chooser_.list(); }

void App::accept_picker() {
    if (!chooser_.is_open()) return;
    std::error_code ec;
    fs::path chosen;
    const int selected = chooser_.selected();
    bool listed = selected >= 0 &&
                  selected < static_cast<int>(chooser_.entries().size()) &&
                  !chooser_.entries()[static_cast<size_t>(selected)].directory;

    if (listed) {
        chosen = fs::path(chooser_.folder()) /
                 chooser_.entries()[static_cast<size_t>(selected)].name;
    } else if (!chooser_.path().empty()) {
        chosen = fs::path(chooser_.path());
        // A folder is somewhere to look in the image picker, and the answer in the folder
        // picker — descending there is what made "Use This Folder" do nothing.
        if (chooser_.kind() == PickerKind::Image && fs::is_directory(chosen, ec)) {
            chooser_.folder() = chosen.wstring();
            chooser_.path() = chooser_.folder();
            chooser_.set_editing(false);
            list_picker();
            invalidate();
            return;
        }
    }
    if (chosen.empty()) {
        chooser_.note() = L"Type a path, or choose one from the list.";
        invalidate();
        return;
    }
    if (chooser_.kind() == PickerKind::Folder) {
        if (!fs::is_directory(chosen, ec)) {
            chooser_.note() = L"That folder does not exist.";
            invalidate();
            return;
        }
    } else if (!fs::is_regular_file(chosen, ec)) {
        chooser_.note() = L"That file does not exist.";
        invalidate();
        return;
    }

    auto accept = chooser_.take_accept();
    close_picker();
    if (accept) accept(chosen);
}

}  // namespace nla
