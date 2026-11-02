// Asking the player for a folder or a file: the platform's own chooser, and the launcher's
// own picker for the systems that have none.

#include "picker_screen.h"

#include "util.h"

#include <algorithm>

namespace nla {

namespace {

std::wstring picker_join(const std::wstring& folder, const std::wstring& name) {
    return (fs::path(folder) / name).wstring();
}

}  // namespace

void PickerScreen::choose_path(PickerKind kind, const std::wstring& title,
                               const fs::path& start,
                               std::function<void(const fs::path&)> accept) {
    auto answer = host_.native_chooser(kind, title, start);
    if (answer.shown) {
        if (answer.path && accept) accept(*answer.path);
        host_.invalidate();
        return;
    }
    // A system chooser that would not open is not what was asked for, so it is said rather
    // than passed over: the list that follows would otherwise look like a regression.
    if (answer.refused) {
        host_.log(L"[launcher] the system chooser did not open; using the launcher's own\n");
    }
    chooser_.open(kind, title, start, std::move(accept));
    open();
}

void PickerScreen::open() {
    host_.take_over();
    host_.text_input(true);
    host_.invalidate();
}

void PickerScreen::close() {
    chooser_.close();
    host_.text_input(false);
    host_.invalidate();
}

void PickerScreen::cancel() {
    host_.launch_cancelled();
    close();
}

void PickerScreen::list() { chooser_.list(); }

void PickerScreen::descend(const fs::path& folder) {
    chooser_.folder() = folder.wstring();
    chooser_.path() = chooser_.folder();
    chooser_.set_editing(false);
    list();
    host_.invalidate();
}

void PickerScreen::accept() {
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
            descend(chosen);
            return;
        }
    }
    if (chosen.empty()) {
        chooser_.note() = L"Type a path, or choose one from the list.";
        host_.invalidate();
        return;
    }
    if (chooser_.kind() == PickerKind::Folder) {
        if (!fs::is_directory(chosen, ec)) {
            chooser_.note() = L"That folder does not exist.";
            host_.invalidate();
            return;
        }
    } else if (!fs::is_regular_file(chosen, ec)) {
        chooser_.note() = L"That file does not exist.";
        host_.invalidate();
        return;
    }

    auto answer = chooser_.take_accept();
    close();
    if (answer) answer(chosen);
}

bool PickerScreen::key(ScreenKey key) {
    switch (key) {
        case ScreenKey::Escape:
            cancel();
            return true;
        case ScreenKey::Enter:
            accept();
            return true;
        case ScreenKey::Tab:
            chooser_.set_editing(!chooser_.editing());
            host_.invalidate();
            return true;
        case ScreenKey::Backspace:
            if (chooser_.editing() && !chooser_.path().empty()) {
                chooser_.path().pop_back();
                // A UTF-16 surrogate pair must not be split.
                if (sizeof(wchar_t) == 2 && !chooser_.path().empty() &&
                    chooser_.path().back() >= 0xD800 && chooser_.path().back() <= 0xDBFF) {
                    chooser_.path().pop_back();
                }
                host_.invalidate();
            }
            return true;
        case ScreenKey::Paste: {
            auto text = host_.clipboard();
            if (!text.empty()) {
                chooser_.set_editing(true);
                chooser_.path() += text;
                host_.invalidate();
            }
            return true;
        }
        case ScreenKey::Up:
            if (!chooser_.entries().empty()) {
                chooser_.set_selected(std::max(0, chooser_.selected() - 1));
                host_.invalidate();
            }
            return true;
        case ScreenKey::Down:
            if (!chooser_.entries().empty()) {
                chooser_.set_selected(std::min(
                    static_cast<int>(chooser_.entries().size()) - 1, chooser_.selected() + 1));
                host_.invalidate();
            }
            return true;
    }
    return false;
}

bool PickerScreen::text(const std::wstring& typed) {
    chooser_.set_editing(true);
    chooser_.path() += typed;
    host_.invalidate();
    return true;
}

bool PickerScreen::wheel(int notches) {
    chooser_.set_offset(std::max(0, chooser_.offset() - notches));
    host_.invalidate();
    return true;
}

bool PickerScreen::handles(Id id) const {
    return id == Id::PickerUp || id == Id::PickerPath || id == Id::PickerAccept ||
           id == Id::PickerCancel || id >= Id::PickerEntry;
}

void PickerScreen::activate(Id id) {
    const int index = static_cast<int>(id) - static_cast<int>(Id::PickerEntry);
    if (id == Id::PickerCancel) {
        cancel();
    } else if (id == Id::PickerAccept) {
        accept();
    } else if (id == Id::PickerUp) {
        fs::path folder(chooser_.folder());
        if (folder.has_parent_path() && folder.parent_path() != folder) {
            descend(folder.parent_path());
        }
    } else if (id == Id::PickerPath) {
        chooser_.set_editing(true);
        host_.invalidate();
    } else if (index >= 0 && index < static_cast<int>(chooser_.entries().size())) {
        const auto& entry = chooser_.entries()[static_cast<size_t>(index)];
        if (entry.directory) {
            fs::path folder(chooser_.folder());
            fs::path next = entry.name == L".." ? folder.parent_path() : (folder / entry.name);
            std::error_code ec;
            if (fs::is_directory(next, ec)) descend(next);
        } else {
            chooser_.set_selected(index);
            chooser_.path() = picker_join(chooser_.folder(), entry.name);
            chooser_.set_editing(true);
            host_.invalidate();
        }
    }
}

void PickerScreen::paint(const PaintContext& ctx) {
    ctx.painter.fill_rect(RectF(0, 0, ctx.width, ctx.height), rgb(0x000000, 0.62f));
    float panel_w = std::min(760.0f, ctx.width - 60);
    float panel_h = std::min(560.0f, ctx.height - 60);
    Rect panel{(ctx.width - panel_w) / 2, (ctx.height - panel_h) / 2, panel_w, panel_h};
    ctx.painter.fill_round(panel.box(), 16, rgb(0x171a21));
    ctx.painter.stroke_round(panel.box(), 16, rgb(theme::BorderStrong), 1);

    ctx.painter.text(chooser_.title(),
                  RectF(panel.x + 24, panel.y + 18, panel.x + panel.w - 24, panel.y + 46),
                  Face::CardTitle, rgb(theme::Text));

    float left = panel.x + 24;
    float content_w = panel.w - 48;

    Rect field{left, panel.y + 56, content_w - 76, 34};
    ctx.painter.fill_round(field.box(), 8, rgb(chooser_.editing() ? 0x20252d : 0x1b1e25));
    ctx.painter.stroke_round(field.box(), 8,
                          rgb(chooser_.editing() ? theme::Accent : theme::BorderStrong), 1);
    ctx.painter.text(ctx.ui.elide(chooser_.path(), Face::Small, field.w - 24),
                  RectF(field.x + 10, field.y, field.x + field.w - 10, field.y + field.h),
                  Face::Small, rgb(theme::Text), TextAlign::Leading, TextVAlign::Center,
                  false);
    ctx.ui.register_hit(Id::PickerPath, field);
    ctx.ui.button(Id::PickerUp, Rect{field.x + field.w + 10, field.y, 66, 34}, L"Up", false, true);

    Rect list{left, panel.y + 100, content_w, panel.h - 100 - 76};
    ctx.painter.fill_round(list.box(), 10, rgb(theme::Log));
    ctx.painter.stroke_round(list.box(), 10, rgb(0x242932), 1);

    const float row_height = 26.0f;
    int visible = std::max(1, static_cast<int>((list.h - 12) / row_height));
    int maximum_offset = std::max(0, static_cast<int>(chooser_.entries().size()) - visible);
    chooser_.set_offset(std::min(chooser_.offset(), maximum_offset));
    for (int row = 0; row < visible; ++row) {
        int index = chooser_.offset() + row;
        if (index >= static_cast<int>(chooser_.entries().size())) break;
        const auto& entry = chooser_.entries()[static_cast<size_t>(index)];
        Rect item{list.x + 6, list.y + 6 + row * row_height, list.w - 12, row_height};
        Id id = static_cast<Id>(static_cast<int>(Id::PickerEntry) + index);
        if (chooser_.selected() == index) {
            ctx.painter.fill_round(item.box(), 6, rgb(0x2b3340));
        } else if (ctx.ui.hover() == id) {
            ctx.painter.fill_round(item.box(), 6, rgb(0x222732));
        }
        std::wstring label = entry.name;
        if (entry.directory) label = entry.name == L".." ? L"Parent folder" : label + L"/";
        ctx.painter.text(label, RectF(item.x + 10, item.y, item.x + item.w - 10,
                                  item.y + item.h),
                      Face::Small, entry.directory ? rgb(0xc8cede) : rgb(theme::Text),
                      TextAlign::Leading, TextVAlign::Center, false);
        ctx.ui.register_hit(id, item);
    }
    if (chooser_.entries().empty()) {
        ctx.painter.text(chooser_.kind() == PickerKind::Image
                          ? L"No disc images here. Type the path, or drop the image on the "
                            L"window."
                          : L"No sub-folders here. Type the path, or use this folder.",
                      RectF(list.x + 14, list.y + 10, list.x + list.w - 14, list.y + 34),
                      Face::Small, rgb(theme::Muted));
    }

    std::wstring note = chooser_.note();
    if (note.empty()) {
        note = chooser_.kind() == PickerKind::Folder ? L"Choose this folder, or open a sub-folder."
                                             : L"Choose a disc image, or type its path.";
    }
    ctx.painter.text(note,
                  RectF(left, panel.y + panel.h - 66, left + content_w - 300,
                       panel.y + panel.h - 44),
                  Face::Tiny, chooser_.note().empty() ? rgb(theme::Muted) : rgb(theme::Warn));

    ctx.ui.button(Id::PickerCancel,
           Rect{panel.x + panel.w - 24 - 100, panel.y + panel.h - 52, 100, 34}, L"Cancel",
           false, true);
    ctx.ui.button(Id::PickerAccept,
           Rect{panel.x + panel.w - 24 - 100 - 8 - 176, panel.y + panel.h - 52, 176, 34},
           chooser_.kind() == PickerKind::Folder ? L"Use This Folder" : L"Choose Image", true, true);
}

}  // namespace nla
