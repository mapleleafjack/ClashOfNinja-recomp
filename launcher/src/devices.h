#pragma once

#include <string>
#include <vector>

#include "ui.h"

namespace nla {

// A controller SDL reports, as the game names it, and the pad it plays as: 1 to 4, or 0 for
// the first one free.
struct DeviceRow {
    std::wstring name;
    int nth = 1;
    int pad = 0;
};

// Which controller plays as which console pad, and the menu that changes it.
//
// The game is asked for the list, because its own SDL is what sees the devices and it names
// them exactly as the pad pins in controls.txt do. Deciding who plays as whom is still the
// launcher's: the pins the game reports are a starting point, not the answer.
class DeviceMap {
  public:
    // Forgets the list, and puts the keyboard back on the first pad.
    void clear();

    // Reads what `--list-devices` printed: a `device` row per controller, and the pad the
    // keyboard plays as.
    void load(const std::wstring& listing);
    // The same list from the caller, for the renders, which have no game to ask.
    void set_devices(std::vector<DeviceRow> rows) { devices_ = std::move(rows); }

    const std::vector<DeviceRow>& devices() const { return devices_; }
    const std::wstring& note() const { return note_; }
    // Why the list is empty, when it is.
    void set_note(std::wstring note) { note_ = std::move(note); }

    int keyboard_pad() const { return keyboard_pad_; }

    // The pad each controller plays as: the pins first, in the order the game lists the
    // devices, then the pads still free. A pin to a pad another device has is passed over,
    // and the keyboard's pad is not offered, so one pad is one device.
    std::vector<int> resolved_pads() const;
    int player_device(int player) const;
    bool player_is_keyboard(int player) const;
    // What a player's row shows: the device playing as that pad, or the muted "Not set".
    std::wstring player_device_label(int player) const;
    // What a player's row offers: nothing, the keyboard, then each controller the game sees.
    std::vector<std::wstring> pad_options() const;

    // A pick takes the pad from whatever was on it, and the keyboard, which always plays as
    // one pad, moves to the first pad no controller is pinned to when a controller takes its
    // own.
    void assign(int player, int choice);

    // The row whose menu is open, 0 for Player 1; -1 for none. The boxes are kept so a row
    // near the bottom of the panel can open its menu upwards.
    int menu_row() const { return menu_row_; }
    void set_menu_row(int row) { menu_row_ = row; }
    const Rect& menu_anchor() const { return menu_anchor_; }
    void set_menu_anchor(const Rect& rect) { menu_anchor_ = rect; }
    const Rect& menu_panel() const { return menu_panel_; }
    void set_menu_panel(const Rect& rect) { menu_panel_ = rect; }

  private:
    std::vector<DeviceRow> devices_;
    std::wstring note_;
    int keyboard_pad_ = 1;
    int menu_row_ = -1;
    Rect menu_anchor_{};
    Rect menu_panel_{};
};

}  // namespace nla
