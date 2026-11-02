#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

#include "codes.h"
#include "project_state.h"
#include "screen.h"

namespace nla {

// The Geckos panel: the codes a game can be played with, chosen one at a time, and the box a
// block pasted from an editor goes into.
//
// A mod's codes come and go with the mod. These do not: each is switched on by itself, kept
// in geckos/<DISCID>.txt beside the games, and written into the same mods.gecko/mods.code the
// mods write, so the runtime and the recompiler need to know nothing about them.
class GeckosScreen : public Screen {
  public:
    GeckosScreen(ScreenHost& host, ProjectState& model) : host_(host), model_(model) {}

    // Joins the fetch's thread. The window calls this before it goes away, because the
    // answer it posts would otherwise arrive at an object that is no longer there.
    void shutdown();
    // Re-reads the disc's codes and what settings remembers about them.
    void refresh();

    void open();
    void toggle(const std::wstring& name);
    void apply();
    void fetch_online();

    // The checked codes, read back from this disc's own file, for the overlay the mods
    // write: a code a mod also names must be the disc's words, not the guide's.
    std::vector<CodeEntry> enabled_entries() const;

    // The box takes the keyboard while it has focus, the way the picker's field does.
    bool capturing() const override { return editing_; }
    bool key(ScreenKey key) override;
    bool text(const std::wstring& typed) override;

    // The box loses focus with the panel, so the window stops taking text.
    void stop_editing();

    // The list scrolls under the wheel while the panel is showing, three rows a notch.
    void scroll(int notches);

    bool handles(Id id) const override;
    void activate(Id id) override;

    // What the drawing asks. The selection is read-only from outside: only Apply changes it.
    const CodeSelection& codes() const { return codes_; }
    const std::wstring& note() const { return note_; }
    const std::wstring& online_label() const { return online_label_; }
    const std::wstring& edit() const { return edit_; }
    bool fetching() const { return fetching_; }
    bool editing() const { return editing_; }
    // The table as the panel draws it: the rows, the note under them and the buttons. The
    // frame around it is the window's, because every page over the main one gets the same.
    void paint_table(const PaintContext& ctx, const Rect& panel, bool busy);
    std::size_t offset() const { return offset_; }
    std::size_t rows_fit() const { return rows_fit_; }
    void set_rows_fit(std::size_t rows) { rows_fit_ = rows; }
    // A frame with the room already measured: the offset cannot run past the last page of
    // rows, or the table would scroll itself empty.
    void clamp_offset();

  private:
    void describe_empty();

    ScreenHost& host_;
    ProjectState& model_;
    CodeSelection codes_;
    std::size_t rows_fit_ = 0;
    std::size_t offset_ = 0;
    bool fetching_ = false;
    // The guide's list is asked for once per disc, without being asked: a player opening the
    // panel should see it rather than a link they have to find.
    bool fetch_tried_ = false;
    bool fetch_failed_ = false;
    std::wstring note_;
    std::wstring online_label_;
    std::wstring edit_;
    bool editing_ = false;
    std::thread thread_;
    std::atomic_bool stop_{false};
};

}  // namespace nla
