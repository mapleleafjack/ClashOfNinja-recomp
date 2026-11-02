#pragma once

#include <array>
#include <string>
#include <vector>

#include "devices.h"
#include "project_state.h"
#include "screen.h"

namespace nla {

// The Players panel: a row per console pad, and the device that plays as it.
//
// The game is asked for the list of controllers, because its own SDL is what sees them and
// it names them exactly as the pad pins in controls.txt do. Deciding who plays as whom is
// the launcher's, and the answer is written back into that same file.
class PlayersScreen : public Screen {
  public:
    PlayersScreen(ScreenHost& host, ProjectState& model) : host_(host), model_(model) {}

    void open();
    void refresh();
    // Writes the picks into the game's controls.txt, which is what the runtime reads.
    void write_pins();
    // The menu over a row is closed whenever something else takes the page.
    void close_menu() { map_.set_menu_row(-1); }
    // Whether a row's list is open, which is the one thing drawn over the panel.
    bool menu_open() const { return map_.menu_row() >= 0; }

    // The devices the game sees, for the page that starts it: one pad per controller, and
    // never fewer than the pads this panel names.
    const std::vector<DeviceRow>& devices() const { return map_.devices(); }

    bool handles(Id id) const override;
    void activate(Id id) override;

    // The rows as the panel draws them, and the device list the menu over one of them
    // offers. The frame around both is the window's.
    void paint_table(const PaintContext& ctx, const Rect& panel, bool busy);
    // The list a row opens, drawn over the panel and flipped up when the row is near its
    // floor.
    void paint_menu(const PaintContext& ctx);

  private:
    ScreenHost& host_;
    ProjectState& model_;
    DeviceMap map_;
    // Where each row was drawn: the menu opens against its own row, so a row near the
    // bottom of the panel opens upwards instead of off it.
    std::array<Rect, kPads> row_rects_{};
    Rect panel_rect_{};
};

}  // namespace nla
