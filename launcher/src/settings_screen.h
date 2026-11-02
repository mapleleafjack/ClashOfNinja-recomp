#pragma once

#include <string>

#include "project_state.h"
#include "screen.h"

namespace nla {

class UpdateScreen;

// The Settings page: which controllers play as which player, the scale the window opens at,
// and whether the launcher looks for a newer release when it starts.
//
// The frame - the scrim, the box, its title, Close and Done - stays the window's, the way
// every other panel's does, because Done is what leaves the page. What moved is the three
// sections inside it and the list the scale opens, so this page owns everything in the box
// except the box.
//
// The UPDATES section reads the update page's line rather than keeping one of its own, so
// the two cannot disagree about what the last check found; the button that starts one is
// that page's to answer, so it is left to the window that holds both.
class SettingsScreen : public Screen {
  public:
    SettingsScreen(ScreenHost& host, ProjectState& model);

    // The line under the sections: what the launcher says after it wrote the settings file.
    // It is written by the update page, which is the only thing that has anything to say.
    void note(const std::wstring& text) { note_ = text; }
    void clear_note() { note_.clear(); }

    // The sections inside the panel. The window has already drawn the frame and hands over
    // the box it drew, so the page lays itself out against the same rectangle.
    void paint(const PaintContext& ctx, const Rect& panel, const UpdateScreen& update, bool busy);

    // The scale list, drawn over the page once the combo has been clicked. It belongs to
    // this page rather than to the overlay because the control it hangs from is only ever
    // on this page.
    bool menu_open() const { return menu_open_; }
    void close_menu() { menu_open_ = false; }
    void paint_menu(const PaintContext& ctx);

    bool handles(Id id) const override;
    void activate(Id id) override;

  private:
    ScreenHost& host_;
    ProjectState& model_;

    std::wstring note_;
    // Where the combo was drawn, recorded as the page paints rather than read back out of
    // the window's hit list, so the list lands under the control it belongs to.
    Rect scale_rect_{};
    bool menu_open_ = false;
};

}  // namespace nla
