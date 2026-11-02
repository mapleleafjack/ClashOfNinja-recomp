#pragma once

#include <string>
#include <vector>

#include "mods.h"
#include "screen.h"

namespace nla {

class GeckosScreen;
class PickerScreen;

// The Mods panel: the mods a game's disc allows, the patch it is played with, and the overlay
// the runtime reads.
//
// Two of the things it needs are other pages rather than the window: the picker, because
// choosing a patch asks for a file, and the Geckos page, because the overlay it writes holds
// the codes switched on there as well as its own mods. Those are held as the pages they are,
// so what this depends on is a type rather than another callback.
class ModsScreen : public Screen {
  public:
    ModsScreen(ScreenHost& host, PickerScreen& picker, GeckosScreen& geckos);

    // Re-reads what this disc offers and what settings remembers about it.
    void refresh();
    void open();
    void toggle(const std::wstring& id);
    void choose_patch();
    void stage_patch(const fs::path& delta);
    void apply();
    // The overlay the runtime reads, rewritten from what is checked here and in Geckos.
    // Called by the re-sync, so it runs often and says nothing unless something changed.
    void write_overlay();

    // The line under the table: how much of it is on.
    std::wstring summary() const;

    bool handles(Id id) const override;
    void activate(Id id) override;

    // What the drawing asks. The selection is read-only from outside: only Apply changes it.
    const ModSelection& selection() const { return selection_; }
    const std::wstring& note() const { return note_; }
    const std::wstring& patch_note() const { return patch_note_; }

  private:
    void refresh_patch();
    void open_folder(const fs::path& path);
    void download(const std::vector<std::wstring>& ids);
    void apply_now();
    void commit(const fs::path& delta, const PatchFit& fit);

    ScreenHost& host_;
    PickerScreen& picker_;
    GeckosScreen& geckos_;

    ModSelection selection_;
    std::wstring note_;
    // What Activity has already been told about the overlay and the catalog, so a re-sync
    // that changed nothing does not say it again.
    std::wstring log_;
    std::wstring catalog_log_;
    fs::path patch_delta_;
    PatchFit patch_fit_;
    std::wstring patch_note_;
};

}  // namespace nla
