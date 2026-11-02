// The three sections inside the Settings panel, and the list the scale opens.

#include "settings_screen.h"

#include "update_screen.h"
#include "util.h"

namespace nla {

namespace {

std::wstring scale_label(int factor) {
    return format(L"%d×   %d × %d", factor, 640 * factor, 480 * factor);
}

}  // namespace

SettingsScreen::SettingsScreen(ScreenHost& host, ProjectState& model)
    : host_(host), model_(model) {}

void SettingsScreen::paint(const PaintContext& ctx, const Rect& panel,
                           const UpdateScreen& update, bool busy) {
    auto& painter = ctx.painter;
    auto& ui = ctx.ui;

    const float left = panel.x + 26;
    const float content_w = panel.w - 52;
    const float right = panel.x + panel.w - 26;
    float y = panel.y + 56;

    // The sections are laid out tight enough that the whole panel fits below the header at
    // the window's smallest size.
    auto section = [&](const std::wstring& text) {
        painter.text(text, RectF(left, y, left + content_w, y + 18), Face::Tiny,
                     rgb(theme::Accent));
        y += 18;
    };
    auto separator = [&] {
        painter.line(left, y, left + content_w, y, rgb(theme::Border));
        y += 10;
    };

    section(L"CONTROLLERS");
    painter.text(ui.elide(L"Which controller plays as which player.", Face::Small,
                          content_w - 130),
                 RectF(left, y + 7, left + content_w - 130, y + 29), Face::Small,
                 rgb(theme::Muted));
    ui.button(Id::SettingsPlayers, Rect{right - 110, y, 110, 32}, L"Players…", false, !busy);
    y += 36;
    separator();

    section(L"RESOLUTION");
    painter.text(L"Window scale", RectF(left, y + 8, left + 96, y + 28), Face::Small,
                 rgb(theme::Muted));
    const Rect scale_rect{left + 96, y, 180, 32};
    scale_rect_ = scale_rect;
    ui.combo(Id::Scale, scale_rect, scale_label(model_.settings().scale), !busy);
    y += 36;
    separator();

    section(L"UPDATES");
    // The words are the update page's; only the colour they are read in is this page's.
    const std::wstring update_line = update.line();
    Color update_color = rgb(theme::Muted);
    switch (update.state()) {
        case UpdateScreen::State::UpToDate:
            update_color = rgb(theme::Ok);
            break;
        case UpdateScreen::State::Available:
            update_color = rgb(theme::Accent);
            break;
        case UpdateScreen::State::Failed:
            update_color = rgb(theme::Warn);
            break;
        case UpdateScreen::State::Checking:
        case UpdateScreen::State::Never:
            break;
    }
    painter.text(ui.elide(update_line, Face::Small, content_w - 250),
                 RectF(left, y + 7, left + content_w - 250, y + 29), Face::Small,
                 update_color);
    ui.button(Id::SettingsCheckNow, Rect{right - 100, y, 100, 32}, L"Check now", false,
              !busy && update.state() != UpdateScreen::State::Checking);
    if (update.state() == UpdateScreen::State::Available) {
        ui.button(Id::SettingsInstallUpdate, Rect{right - 100 - 8 - 128, y, 128, 32},
                  L"Install update", true, !busy);
    }
    y += 36;
    ui.checkbox(Id::SettingsToggleUpdates, left, y + 4, L"Check when the launcher starts",
                model_.settings().check_updates, !busy);
    y += 24;

    if (!note_.empty()) {
        painter.text(note_, RectF(left, y, left + content_w, y + 22), Face::Tiny,
                     rgb(theme::Ok));
    }
}

void SettingsScreen::paint_menu(const PaintContext& ctx) {
    Rect menu{scale_rect_.x, scale_rect_.y + scale_rect_.h + 6, scale_rect_.w,
              4 * 32 + 8};
    ctx.painter.fill_round(menu.box(), 10, rgb(0x1b1e25));
    ctx.painter.stroke_round(menu.box(), 10, rgb(theme::BorderStrong), 1);
    for (int index = 0; index < 4; ++index) {
        Rect item{menu.x + 4, menu.y + 4 + index * 32.0f, menu.w - 8, 32};
        Id id = static_cast<Id>(static_cast<int>(Id::Scale1) + index);
        if (ctx.ui.hover() == id) ctx.painter.fill_round(item.box(), 6, rgb(0x2b3340));
        ctx.painter.text(scale_label(index + 1),
                         RectF(item.x + 10, item.y, item.x + item.w - 10, item.y + item.h),
                         Face::Small,
                         model_.settings().scale == index + 1 ? rgb(theme::Accent)
                                                             : rgb(theme::Text),
                         TextAlign::Leading, TextVAlign::Center, false);
        ctx.ui.register_hit(id, item);
    }
}

bool SettingsScreen::handles(Id id) const {
    // Check now, Install update and Players… are drawn in this panel but answered by the
    // page that owns what they do, so they are not this page's ids.
    return id == Id::SettingsToggleUpdates || (id >= Id::Scale && id <= Id::Scale4);
}

void SettingsScreen::activate(Id id) {
    switch (id) {
        case Id::SettingsToggleUpdates:
            model_.settings().check_updates = !model_.settings().check_updates;
            model_.settings().save();
            break;
        case Id::Scale:
            menu_open_ = !menu_open_;
            break;
        case Id::Scale1:
        case Id::Scale2:
        case Id::Scale3:
        case Id::Scale4:
            model_.settings().scale = static_cast<int>(id) - static_cast<int>(Id::Scale1) + 1;
            menu_open_ = false;
            model_.settings().save();
            break;
        default:
            return;
    }
    host_.invalidate();
}

}  // namespace nla
