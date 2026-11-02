// What the launcher re-reads when the folder it is working in may have changed under it.
//
// This is not a page. It touches the mods, the codes, the overlay the runtime reads and the
// status the covers are drawn from, in an order that matters, so it is kept apart from the
// pages rather than inside the one whose file it used to sit in.

#include "app.h"

namespace nla {

void App::refresh() {
    // The mods come first: their files and codes are what the game's status is measured
    // against, so the status is read after they have been written.
    refresh_mods();
    // The codes chosen on their own are read back here too: the program the runtime runs
    // holds the mods' codes and these, so both lists must be current before it is written.
    refresh_geckos();
    write_mod_overlay();
    state_.reload_status();
    invalidate();
}

}  // namespace nla
