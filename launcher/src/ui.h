#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "painter.h"

namespace nla {

// The console's four controller sockets: the pads a game polls for players.
constexpr int kPads = 4;
// How many code rows the Geckos panel draws at once. A disc's guide can hold far more, so
// the panel scrolls rather than growing past the window.
constexpr int kCodeRows = 12;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    RectF box() const { return RectF{x, y, x + w, y + h}; }
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    Rect inset(float amount) const {
        return {x + amount, y + amount, w - 2 * amount, h - 2 * amount};
    }
};

enum class Id {
    None,
    Launch,
    Stop,
    LogToggle,
    CopyLog,
    ClearLog,
    OpenSettings,
    SettingsClose,
    SettingsToggleUpdates,
    SettingsCheckNow,
    SettingsInstallUpdate,
    UpdateBanner,
    Scale,
    Scale1,
    Scale2,
    Scale3,
    Scale4,
    SettingsPlayers,
    SettingsRefreshDevices,
    ModsFolder,
    ModRow,  // a mod in the Mods table; the rest follow it
    ModRowEnd = ModRow + 12,
    ModApply,   // download what is missing and write the checked mods
    ModChoose,  // add a disc patch from this machine to the table
    PadRow,                       // a player row in the Players panel: +0 Player 1, then 2 to 4
    PadRowEnd = PadRow + 4,
    PadOption,                    // what a player row offers: +0 Not set, +1 the keyboard, then
                                  // the controllers
    PadOptionEnd = PadOption + 20,
    OverlayAction,
    OverlayCancel,
    OverlaySecondary,
    PickerUp,
    PickerPath,
    PickerAccept,
    PickerCancel,
    Play,         // start the game the page has selected
    Mods,         // change what that game plays
    Geckos,       // switch that game's Gecko codes on and off
    CodeApply,    // write the checks, and any code pasted into the box
    CodeRefresh,  // ask for the guide's list again
    CodeBox,      // the box a block of codes is pasted into
    // Ranges whose ids are computed as base + index must come last, so nothing follows them.
    GameCover,    // the first game's cover on the main page; the rest follow it
    GameCoverEnd = GameCover + 32,
    CodeRow,  // a code in the Geckos table; the rest follow it
    CodeRowEnd = CodeRow + kCodeRows,
    PickerEntry,  // the first entry row; the rest follow it
};

enum class Pill { Muted, Ok, Warn, Bad };

// Where the mouse can be pressed, recorded as a frame is drawn and asked about after it.
// The last one registered wins, so a menu drawn over a panel takes the click.
struct Hit {
    Rect rect;
    Id id;
};

// The drawing primitives the launcher's panels are built from, and the hit list they leave
// behind. It holds no application state: every value a widget draws is passed in, so a
// panel can be laid out and clicked without a window or a game.
class UiContext {
  public:
    UiContext(Painter& painter) : painter_(painter) {}

    // Forgets the last frame's hit list. Hover and press survive, because they describe the
    // mouse rather than the frame.
    void begin_frame();
    // Where `id` was drawn last frame, for the panel a click on it opens.
    std::optional<Rect> rect_of(Id id) const;
    // The frame's hit list in the order it was drawn, for the tests that click a control.
    const std::vector<Hit>& hits() const { return hits_; }

    Id hover() const { return hover_; }
    void set_hover(Id id) { hover_ = id; }
    Id pressed() const { return pressed_; }
    void set_pressed(Id id) { pressed_ = id; }

    // `mark` draws the button instead of a label: the square buttons a narrow column puts
    // beside Play, which have room for a shape but not for a word.
    void button(Id id, const Rect& rect, const std::wstring& label, bool primary, bool enabled,
                bool danger = false, Face face = Face::Button,
                const std::function<void(const Rect&, Color)>& mark = {});
    void link(Id id, const Rect& rect, const std::wstring& label, bool enabled = true);
    void checkbox(Id id, float x, float y, const std::wstring& label, bool checked,
                  bool enabled = true);
    // A box showing the value it opens a list to change, with a chevron at its right. A
    // value that is unset reads muted, which is how an empty player row says "Not set".
    void combo(Id id, const Rect& rect, const std::wstring& value, bool enabled,
               bool muted = false);
    void pill(const Rect& rect, const std::wstring& text, Pill state);
    void card_box(const Rect& rect);

    float text_width(const std::wstring& text, Face face);
    std::wstring elide(const std::wstring& text, Face face, float width);
    // The same for a path, which is cut in the middle so both the folder and the name stay.
    std::wstring elide_middle(const std::wstring& text, Face face, float width);

    void register_hit(Id id, const Rect& rect);
    Id hit_at(float x, float y) const;

  private:
    Painter& painter_;
    Id hover_ = Id::None;
    Id pressed_ = Id::None;
    std::vector<Hit> hits_;
};

}  // namespace nla
