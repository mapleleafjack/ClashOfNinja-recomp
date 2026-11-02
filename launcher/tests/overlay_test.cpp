// Which page covers the main one, and what it takes with it when it opens or closes.
// Only one page is ever showing, so a screen can no longer be left drawn under another one
// because a caller forgot a line; the overlay is a plain value with no window and no
// painter, so that rule is checked here rather than through the drawing. Run by ctest.

#include "ui.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

}  // namespace

int main() {
    // A fresh overlay shows nothing.
    {
        nla::Overlay overlay;
        check(!overlay.showing(), "a new overlay shows no page");
        check(!overlay.is_overlay(), "a new overlay is not an overlay page");
        check(overlay.is(nla::OverlayPage::None), "a new overlay is on no page");
    }

    // Settings is a page of its own, and a table replaces it: the launcher used to keep
    // these in separate flags, so opening one left the other drawn underneath.
    {
        nla::Overlay overlay;
        overlay.show_page(nla::OverlayPage::Settings);
        check(overlay.showing(), "Settings is a page that is showing");
        check(overlay.is(nla::OverlayPage::Settings), "Settings is the page showing");
        check(!overlay.is_overlay(), "Settings answers to its own controls, not the overlay's");

        overlay.show_panel(nla::OverlayPage::Mods, L"Mods");
        check(overlay.is(nla::OverlayPage::Mods), "a table replaces Settings");
        check(!overlay.is(nla::OverlayPage::Settings), "Settings is no longer showing");
        check(overlay.is_overlay() && overlay.is_table(), "a table is an overlay page");
    }

    // The other order too, which is the one a message from a running step takes.
    {
        nla::Overlay overlay;
        overlay.show_page(nla::OverlayPage::Settings);
        overlay.show_message(L"Updates", L"Something happened", L"OK");
        check(overlay.is(nla::OverlayPage::Message), "a message replaces Settings");
        check(!overlay.is(nla::OverlayPage::Settings), "Settings is gone when a message shows");
        check(overlay.is_overlay(), "a message is an overlay page");
        check(!overlay.is_table(), "a message is not a table");
    }

    // Each table is an overlay page, and only one at a time.
    {
        nla::Overlay overlay;
        overlay.show_panel(nla::OverlayPage::Mods, L"Mods");
        overlay.show_panel(nla::OverlayPage::Geckos, L"Geckos");
        check(overlay.is(nla::OverlayPage::Geckos) && !overlay.is(nla::OverlayPage::Mods),
              "opening Geckos closes Mods");
        overlay.show_panel(nla::OverlayPage::Players, L"Players");
        check(overlay.is(nla::OverlayPage::Players) && !overlay.is(nla::OverlayPage::Geckos),
              "opening Players closes Geckos");
    }

    // Closing leaves nothing showing, which is what puts the main page back.
    {
        nla::Overlay overlay;
        overlay.show_page(nla::OverlayPage::Settings);
        overlay.close();
        check(!overlay.showing(), "closing leaves no page showing");
    }

    // Closing clears what the old page would have done, so a button pressed twice cannot
    // run its work twice.
    {
        nla::Overlay overlay;
        bool ran = false;
        overlay.show_confirm(L"Replace the disc?", L"Text", L"Replace", [&] { ran = true; });
        overlay.close();
        check(overlay.take_action() == nullptr, "closing drops the button's work");
        check(overlay.take_second() == nullptr, "closing drops the second button's work");
        check(!ran, "closing runs nothing");
    }

    // The action is handed back before the page closes, which is how a button both closes
    // and goes on to do its work.
    {
        nla::Overlay overlay;
        bool ran = false;
        overlay.show_confirm(L"Replace the disc?", L"Text", L"Replace", [&] { ran = true; });
        auto action = overlay.take_action();
        check(static_cast<bool>(action), "the action comes back out of the page");
        if (action) action();
        check(ran, "the action runs when it is taken out");
    }

    std::printf("%s\n", failures == 0 ? "overlay: all checks passed" : "overlay: FAILURES");
    return failures == 0 ? 0 : 1;
}
