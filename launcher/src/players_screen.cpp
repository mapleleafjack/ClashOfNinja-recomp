// Who plays as whom: a row per console pad, and the device that plays as it.

#include "players_screen.h"

#include "util.h"

#include <algorithm>
#include <map>

namespace nla {

void PlayersScreen::open() {
    refresh();
    host_.show_panel(OverlayPage::Players, L"Players");
    host_.invalidate();
}

void PlayersScreen::refresh() {
    map_.clear();
    // A stand-in list for the renders: the same devices whatever this machine has
    // plugged in, so the drawing is the same everywhere it is checked.
    if (host_.rendering()) {
        map_.set_devices(
            {{L"Xbox Wireless Controller", 1, 0}, {L"PS4 Controller", 1, 2}});
        return;
    }
    std::error_code ec;
    // The game answers for itself: its own SDL sees the devices, and it names them exactly
    // as the pad pins in controls.txt do.
    if (!model_.project() || !fs::exists(model_.project()->binary(), ec)) {
        map_.set_note(
            L"Build the game first: the launcher asks it which controllers it sees.");
        return;
    }
    std::string out;
    if (!capture_output(model_.project()->binary().wstring(),
                        {L"--list-devices", L"--controls=" + model_.project()->controls().wstring()}, &out))
        return;
    map_.load(widen(out));
    if (map_.devices().empty())
        map_.set_note(L"No controller found. Plug one in and press Refresh.");
}

// The pads this picker owns are the `pad = "<device>" <n>` and `keyboard_pad = <n>` lines.
// Everything else in controls.txt is the player's own mapping and is kept as written, and
// so is a pin for a controller that is unplugged right now: it is still who plays next
// time it is plugged in.
void PlayersScreen::write_pins() {
    // A render checks the panel, not the player's disk: it runs with no game to read back
    // from, so its device list is a stand-in and must not reach controls.txt.
    if (!model_.project() || host_.rendering()) return;
    const fs::path path = model_.project()->controls();
    std::wstring text;
    std::error_code ec;
    if (fs::exists(path, ec)) text = read_text_file(path);
    std::map<std::wstring, int> pins;
    std::wstring kept;
    for (const auto& line : split(text, L'\n')) {
        auto words = split(trim(line), L' ');
        if (!words.empty() && words[0] == L"pad") {
            const size_t open = line.find(L'"', line.find(L"pad"));
            const size_t close = line.rfind(L'"');
            if (open != std::wstring::npos && close != std::wstring::npos && close > open) {
                const int pad = std::atoi(narrow(line.substr(close + 1)).c_str());
                if (pad >= 1 && pad <= 4) pins[line.substr(open + 1, close - open - 1)] = pad;
            }
            continue;
        }
        if (!words.empty() && words[0] == L"keyboard_pad") continue;
        kept += line;
        kept += L'\n';
    }
    // What the panel shows is what the game gets: every controller the panel finds a pad
    // for is pinned to it, so the first-free rule has nothing left to decide.
    const std::vector<int> pads = map_.resolved_pads();
    for (size_t i = 0; i < map_.devices().size(); ++i) {
        std::wstring label = map_.devices()[i].name;
        if (map_.devices()[i].nth > 1)
            label += L" #" + std::to_wstring(map_.devices()[i].nth);
        if (pads[i] == 0) pins.erase(label);
        else pins[label] = pads[i];
    }
    if (map_.keyboard_pad() != 1)
        kept += L"keyboard_pad = " + std::to_wstring(map_.keyboard_pad()) + L"\n";
    int highest = 1;
    for (const auto& [label, pad] : pins) {
        kept += L"pad = \"" + label + L"\" " + std::to_wstring(pad) + L"\n";
        highest = std::max(highest, pad);
    }
    if (!write_text_file(path, narrow(kept))) {
        host_.message(L"Player pads", L"The launcher could not write\n\n" + path.wstring());
        return;
    }
    // A pad past the cap would be ignored by the game, so the cap follows the picks.
    model_.settings().players = std::max(model_.settings().players, highest);
    model_.settings().save();
    host_.log(L"[launcher] players: keyboard is player " +
               std::to_wstring(map_.keyboard_pad()) + L"\n");
    for (const auto& [label, pad] : pins)
        host_.log(L"[launcher] players: \"" + label + L"\" is player " +
                   std::to_wstring(pad) + L"\n");
    refresh();  // so the rows show what the game now reads back
    host_.invalidate();
}

bool PlayersScreen::handles(Id id) const {
    if (id >= Id::PadRow && id < Id::PadRowEnd) return true;
    if (id >= Id::PadOption && id < Id::PadOptionEnd) return true;
    return id == Id::SettingsRefreshDevices;
}

void PlayersScreen::activate(Id id) {
    // A menu is open: a choice in it takes the pad, and anything else closes it first and
    // then does its own work.
    if (map_.menu_row() >= 0) {
        const int player = map_.menu_row() + 1;
        const int picked = static_cast<int>(id) - static_cast<int>(Id::PadOption);
        if (id >= Id::PadOption && id < Id::PadOptionEnd &&
            picked < static_cast<int>(map_.pad_options().size())) {
            map_.set_menu_row(-1);
            map_.assign(player, picked);
            write_pins();
            return;
        }
        map_.set_menu_row(-1);
    }
    if (id >= Id::PadRow && id < Id::PadRowEnd) {
        const int row = static_cast<int>(id) - static_cast<int>(Id::PadRow);
        if (row >= 0 && row < kPads) {
            map_.set_menu_row(row);
            map_.set_menu_anchor(row_rects_[static_cast<std::size_t>(row)]);
            host_.invalidate();
            return;
        }
    }
    if (id == Id::SettingsRefreshDevices) {
        refresh();
        host_.invalidate();
    }
}

void PlayersScreen::paint_table(const PaintContext& ctx, const Rect& panel, bool busy) {
    // One row per pad, and the device that plays as it: the picks go to the game's
    // controls.txt, which is what the runtime reads.
    panel_rect_ = panel;
    float left = panel.x + 26;
    float right = panel.x + panel.w - 26;
    float y = panel.y + 60;
    ctx.painter.text(L"Pick the device each player uses. Controllers take the first free pad.",
                     RectF(left, y, right - 70, y + 20), Face::Tiny, rgb(theme::Muted));
    ctx.ui.link(Id::SettingsRefreshDevices, Rect{right - 62, y, 62, 18}, L"Refresh");
    y += 26;
    for (int player = 1; player <= kPads; ++player) {
        const std::wstring value = map_.player_device_label(player);
        ctx.painter.text(ctx.ui.elide(L"Player " + std::to_wstring(player), Face::Small,
                                      right - left - 200),
                         RectF(left, y + 5, right - 190, y + 27), Face::Small,
                         rgb(theme::Text));
        const Rect combo{right - 190, y, 190, 28};
        row_rects_[static_cast<std::size_t>(player - 1)] = combo;
        ctx.ui.combo(static_cast<Id>(static_cast<int>(Id::PadRow) + player - 1), combo, value,
                     !busy, value == L"Not set");
        y += 34;
    }
    if (!map_.note().empty()) {
        ctx.painter.text(ctx.ui.elide(map_.note(), Face::Tiny, right - left),
                         RectF(left, y, right, y + 18), Face::Tiny, rgb(theme::Muted));
    }
}

void PlayersScreen::paint_menu(const PaintContext& ctx) {
    const std::vector<std::wstring> options = map_.pad_options();
    const int count = static_cast<int>(options.size());
    const Rect anchor = map_.menu_anchor();
    Rect menu{anchor.x, anchor.y + anchor.h + 6, anchor.w, count * 30.0f + 8};
    if (menu.y + menu.h > panel_rect_.y + panel_rect_.h - 10) menu.y = anchor.y - 6 - menu.h;
    ctx.painter.fill_round(menu.box(), 10, rgb(0x1b1e25));
    ctx.painter.stroke_round(menu.box(), 10, rgb(theme::BorderStrong), 1);
    const int player = map_.menu_row() + 1;
    int current = 0;
    if (map_.player_is_keyboard(player)) {
        current = 1;
    } else if (const int device = map_.player_device(player); device >= 0) {
        current = 2 + device;
    }
    for (int i = 0; i < count; ++i) {
        Rect item{menu.x + 4, menu.y + 4 + i * 30.0f, menu.w - 8, 30};
        Id id = static_cast<Id>(static_cast<int>(Id::PadOption) + i);
        if (ctx.ui.hover() == id) ctx.painter.fill_round(item.box(), 6, rgb(0x2b3340));
        ctx.painter.text(ctx.ui.elide(options[static_cast<size_t>(i)], Face::Small, item.w - 20),
                         RectF(item.x + 10, item.y, item.x + item.w - 10, item.y + item.h),
                         Face::Small,
                         i == current ? rgb(theme::Accent)
                                      : rgb(i == 0 ? theme::Muted : theme::Text),
                         TextAlign::Leading, TextVAlign::Center, false);
        ctx.ui.register_hit(id, item);
    }
}

}  // namespace nla
