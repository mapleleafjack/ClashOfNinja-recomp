// The mods a game's disc allows, the patch it is played with, and the overlay the
// runtime reads.

#include "mods_screen.h"

#include "catalog.h"
#include "geckos_screen.h"
#include "patches.h"
#include "picker_screen.h"

#include "tools.h"
#include "update.h"
#include "util.h"

#include <algorithm>
#include <memory>

namespace nla {

ModsScreen::ModsScreen(ScreenHost& host, ProjectState& model, PickerScreen& picker,
                       GeckosScreen& geckos)
    : host_(host), model_(model), picker_(picker), geckos_(geckos) {}

namespace {

// The first file under `root` with this name, wherever the archive put it.
bool find_file_named(const fs::path& root, const std::wstring& name, fs::path* found) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) return false;
        if (!it->is_regular_file(ec)) continue;
        if (to_lower(it->path().filename().wstring()) == to_lower(name)) {
            *found = it->path();
            return true;
        }
    }
    return false;
}

// The folder inside an unpacked archive that holds a mod: the one the catalog names, else
// the one named after the mod, else the first one with a mod.txt at its root.
fs::path find_mod_root(const fs::path& root, const std::wstring& id) {
    std::error_code ec;
    if (fs::is_regular_file(root / L"mod.txt", ec)) return root;
    fs::path named;
    fs::path any;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code item_ec;
        if (!it->is_directory(item_ec)) continue;
        if (!fs::is_regular_file(it->path() / L"mod.txt", item_ec)) continue;
        if (any.empty()) any = it->path();
        if (to_lower(it->path().filename().wstring()) == to_lower(id)) {
            named = it->path();
            break;
        }
    }
    return named.empty() ? any : named;
}

bool same_path(const fs::path& a, const fs::path& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec)) {
        const bool same = fs::equivalent(a, b, ec);
        if (!ec) return same;
    }
    return to_lower(a.lexically_normal().generic_wstring()) ==
           to_lower(b.lexically_normal().generic_wstring());
}

struct Fetch {
    std::wstring id;
    std::wstring name;
    std::wstring version;
    std::wstring file;
    std::wstring folder;
    std::wstring url;
    std::wstring owner;
    std::wstring repo;
    std::wstring asset;
    bool patch = false;
};

}  // namespace


// The mods this disc allows: every entry in mods.json that names its ID, and every folder
// beside the games whose mod.txt names it. A mod written for another build is never
// offered, so nothing lands on addresses it was not written for.
void ModsScreen::refresh() {
    // The rows are read from the disk again, but the checks the player has staged are theirs
    // until Apply writes them: a refresh in between - a download finishing, the folder
    // changing under the panel - must not silently drop the choice Apply is about to write.
    const std::vector<std::wstring> staged = selection_.pending();
    const std::wstring staged_patch = selection_.pending_patch();
    selection_.clear();

    // Stand-ins for the renders: the same table and the same picks wherever this is
    // checked, so the drawing does not depend on a machine's disk or network.
    if (host_.rendering()) {
        ModChoice patch;
        patch.id = L"scon4";
        patch.name = L"SCON4";
        patch.version = L"2.1.0";
        patch.author = L"SCON4 team";
        patch.description =
            L"Super Clash of Ninja 4: rebalanced characters, new stages and menus.";
        patch.disc_ids = {L"g4njda"};
        patch.patch = true;
        patch.installed = true;
        patch.known = true;
        patch.delta = L"SCON4-2.1.0.xdelta";
        selection_.entries().push_back(std::move(patch));

        ModChoice first;
        first.id = L"retuned-opening";
        first.name = L"Retuned Opening";
        first.version = L"1.0";
        first.author = L"example";
        first.description = L"Swaps the banner in the opening movie.";
        first.disc_ids = {L"g4njda"};
        first.installed = true;
        first.known = true;
        selection_.entries().push_back(std::move(first));

        ModChoice second;
        second.id = L"alt-colours";
        second.name = L"Alternate Colours";
        second.version = L"0.9";
        second.author = L"example";
        second.description = L"Alternate costumes for the whole roster.";
        second.disc_ids = {L"g4njda"};
        second.installed = true;
        second.known = true;
        selection_.entries().push_back(std::move(second));

        selection_.enabled().push_back(L"retuned-opening");
        selection_.applied_patch() = L"scon4";
        patch_delta_ = L"SCON4-2.1.0.xdelta";
        return;
    }

    if (!model_.project()) return;
    const std::wstring disc_id = model_.project()->disc_id();
    const fs::path patches_dir = patch_directory(model_.project()->root());

    // What the tree carries is the applied patch; a manifest naming a file that is gone,
    // as a download into a temp folder is, falls back to what settings remembers.
    refresh_patch();

    std::wstring catalog_error;
    selection_.catalog() = load_mod_catalog(mod_catalog_path(), &catalog_error);
    if (catalog_error != catalog_log_) {
        catalog_log_ = catalog_error;
        if (!catalog_error.empty()) host_.log(L"[launcher] " + catalog_error + L"\n");
    }

    // The table: the catalog's entries for this disc, the folders beside the games, and the
    // deltas in patches/, merged by id.
    selection_.entries() = collect_mods(selection_.catalog(), model_.project()->mods_dir(), patches_dir, disc_id);

    // What is written now, in the order it was switched on so a later mod wins where two
    // replace the same file.
    auto saved = model_.settings().mods.find(model_.project()->game());
    if (saved != model_.settings().mods.end()) {
        for (const auto& id : saved->second) {
            for (const auto& entry : selection_.entries()) {
                if (entry.patch || entry.id != id) continue;
                selection_.enabled().push_back(id);
                break;
            }
        }
    }

    if (!patch_delta_.empty()) {
        bool matched = false;
        for (const auto& entry : selection_.entries()) {
            if (!entry.patch || !same_path(entry.delta, patch_delta_)) continue;
            selection_.applied_patch() = entry.id;
            matched = true;
            break;
        }
        // A delta the catalog names is that mod, even when it is kept outside patches/ or is
        // a different copy from the one there: a copy this install did not write, or the
        // setting another install of the launcher left behind. It is the entry's own delta,
        // not a second row beside it.
        if (!matched) {
            for (auto& entry : selection_.entries()) {
                if (!entry.patch) continue;
                if (!delta_belongs(entry.id, entry.name, patch_delta_)) continue;
                entry.delta = patch_delta_;
                entry.installed = true;
                selection_.applied_patch() = entry.id;
                matched = true;
                break;
            }
        }
        if (!matched) {
            // A patch chosen earlier and kept elsewhere, whose mod nothing here names, is
            // still a row, so the table can show what is applied and switch it off.
            ModChoice local;
            local.id =
                unique_mod_id(selection_.entries(), L"patch:" + patch_delta_.filename().wstring());
            local.name = patch_delta_.stem().wstring();
            local.patch = true;
            local.installed = true;
            local.delta = patch_delta_;
            selection_.applied_patch() = local.id;
            selection_.entries().push_back(std::move(local));
        }
    }

    // The staged checks, as far as the rebuilt table still has those rows: a mod folder that
    // was deleted, or a delta that has moved, is no longer something Apply could write.
    for (const auto& id : staged) {
        const ModChoice* entry = selection_.find(id);
        if (entry && !entry->patch) selection_.pending().push_back(id);
    }
    if (const ModChoice* entry = selection_.find(staged_patch); entry && entry->patch)
        selection_.pending_patch() = staged_patch;
}

std::wstring ModsScreen::summary() const {
    if (host_.rendering()) return L"2 of 3 on.";
    if (!model_.project()) return L"Set up the game folder first.";
    if (selection_.entries().empty()) return L"No mods for this disc.";
    const std::size_t on = selection_.enabled().size() + (selection_.applied_patch().empty() ? 0 : 1);
    auto text = format(L"%zu of %zu on.", on, selection_.entries().size());
    if (model_.status().stale_code) text += L" A code patch needs a build.";
    return text;
}

void ModsScreen::toggle(const std::wstring& id) {
    if (!selection_.downloadable(id)) return;
    selection_.toggle(id);
    note_.clear();
    host_.invalidate();
}

void ModsScreen::open() {
    refresh();
    selection_.take_from_written();
    note_.clear();
    if (selection_.entries().empty() && !host_.rendering()) {
        note_ =
            model_.project() ? L"No mods for this disc. A mod is a folder in mods/ with a mod.txt "
                       L"naming this disc's ID and a disc/ tree of the files it replaces."
                     : L"Set up the game folder first: the mods are kept beside it.";
    }
    host_.show_panel(OverlayPage::Mods,
                     model_.project() ? L"Mods — " + model_.project()->title() : L"Mods");
    host_.invalidate();
}

// A disc patch replaces the whole disc the game reads, so it is one choice at a time: the
// checked patch row, kept wherever the player keeps the delta, and decoded by the runtime
// as the game asks for its files.
void ModsScreen::refresh_patch() {
    patch_delta_.clear();
    patch_fit_ = {};
    if (!model_.project() || host_.rendering()) return;
    std::error_code ec;
    patch_delta_ = read_patch_delta(model_.project()->disc());
    // A manifest can name a delta a download made in a temp folder, which is not a patch
    // the player still has. Only a file that is here counts as the applied patch.
    if (!patch_delta_.empty() && !fs::is_regular_file(patch_delta_, ec)) patch_delta_.clear();
    if (patch_delta_.empty()) {
        // Settings remembers what was chosen, so a tree that was extracted again gets its
        // patch back without asking for the file a second time.
        auto saved = model_.settings().patches.find(model_.project()->game());
        if (saved != model_.settings().patches.end() && !saved->second.empty() &&
            fs::is_regular_file(fs::path(saved->second), ec)) {
            patch_delta_ = saved->second;
        }
    }
}

void ModsScreen::choose_patch() {
    if (!model_.project()) return;
    fs::path start = patch_delta_.empty() ? model_.project()->disc() : patch_delta_;
    picker_.choose_path(PickerKind::Patch, L"Choose the patch to play with", start,
                [this](const fs::path& chosen) { stage_patch(chosen); });
}

// A delta from this machine joins the table checked, and Apply writes it. A patch is not
// applied on the click that chooses it: the panel has one Apply for everything it lists.
void ModsScreen::stage_patch(const fs::path& delta) {
    if (!model_.project() || delta.empty()) return;
    for (auto& entry : selection_.entries()) {
        if (entry.patch && same_path(entry.delta, delta)) {
            selection_.pending_patch() = entry.id;
            host_.invalidate();
            return;
        }
    }
    ModChoice entry;
    entry.id = unique_mod_id(selection_.entries(), L"patch:" + delta.filename().wstring());
    entry.name = delta.stem().wstring();
    entry.patch = true;
    entry.installed = true;
    entry.delta = delta;
    selection_.entries().push_back(std::move(entry));
    selection_.pending_patch() = selection_.entries().back().id;
    note_.clear();
    host_.invalidate();
}

// Applies the checks the panel shows: first the mods this machine does not have are
// fetched, then the overlay and the patch are written. Nothing is written before every
// chosen download is here, so a failed download leaves the game as it was.
void ModsScreen::apply() {
    if (!model_.project() || host_.busy()) return;
    if (host_.rendering()) {
        // A render has no disk to write: it checks that the click reaches the button.
        note_ = L"Applied in a render check only.";
        host_.invalidate();
        return;
    }

    std::vector<std::wstring> missing;
    for (const auto& id : selection_.pending()) {
        const ModChoice* entry = selection_.find(id);
        if (!entry || entry->installed) continue;
        if (entry->known && entry->catalog < selection_.catalog().size() &&
            selection_.catalog()[entry->catalog].downloadable()) {
            missing.push_back(id);
        }
    }
    if (!selection_.pending_patch().empty()) {
        const ModChoice* entry = selection_.find(selection_.pending_patch());
        if (entry && !entry->installed && entry->known &&
            entry->catalog < selection_.catalog().size() &&
            selection_.catalog()[entry->catalog].downloadable()) {
            missing.push_back(entry->id);
        }
    }
    if (!missing.empty()) {
        download(missing);
        return;
    }
    apply_now();
}

// Fetches each chosen mod's archive into a temp folder, unpacks it, and puts the mod's
// folder - or the patch's delta - where the panel looks for it. The work runs in the
// launcher's job, so it reports progress and Cancel stops it like any other step.
void ModsScreen::download(const std::vector<std::wstring>& ids) {
    if (!model_.project()) return;
    const fs::path patches_dir = patch_directory(model_.project()->root());
    const fs::path mods_dir = model_.project()->mods_dir();

    std::vector<Task> tasks;
    for (const auto& id : ids) {
        const ModChoice* entry = selection_.find(id);
        if (!entry || entry->catalog >= selection_.catalog().size()) continue;
        const CatalogMod& known = selection_.catalog()[entry->catalog];
        auto fetch = std::make_shared<Fetch>();
        fetch->id = known.id;
        fetch->name = known.name.empty() ? known.id : known.name;
        fetch->version = known.version;
        fetch->file = known.file;
        fetch->folder = known.folder;
        fetch->url = known.url;
        fetch->owner = known.owner;
        fetch->repo = known.repo;
        fetch->asset = known.asset;
        fetch->patch = known.patch;

        auto release = std::make_shared<PatchRelease>();
        const fs::path zip = temp_dir() / (L"NarutoLauncher-" + fetch->id + L".zip");
        const fs::path unpack = temp_dir() / (L"NarutoLauncher-" + fetch->id);

        if (!fetch->owner.empty() && !fetch->repo.empty()) {
            tasks.push_back({L"Asking GitHub for " + fetch->name + L"…",
                             [release, fetch](Job& job) -> int {
                                 PatchSource source{fetch->id, fetch->name, fetch->owner,
                                                    fetch->repo, fetch->asset, fetch->file};
                                 *release = find_patch_release(source);
                                 if (!release->error.empty()) {
                                     job.log(format(L"[launcher] %ls\n",
                                                    release->error.c_str()));
                                     return 1;
                                 }
                                 job.log(format(L"[launcher] %ls %ls: %ls\n",
                                                fetch->name.c_str(),
                                                release->version.c_str(),
                                                release->asset_url.c_str()));
                                 return 0;
                             }});
            tasks.push_back({L"Downloading " + fetch->name + L"…",
                             [release, zip](Job& job) -> int {
                                 std::error_code ec;
                                 fs::remove(zip, ec);
                                 return job.run_process(
                                     curl_tool(),
                                     {L"-fsSL", L"--max-time", L"1800", L"-o", zip.wstring(),
                                      release->asset_url},
                                     temp_dir());
                             }});
        } else {
            tasks.push_back({L"Downloading " + fetch->name + L"…",
                             [fetch, zip](Job& job) -> int {
                                 std::error_code ec;
                                 fs::remove(zip, ec);
                                 return job.run_process(
                                     curl_tool(),
                                     {L"-fsSL", L"--max-time", L"1800", L"-o", zip.wstring(),
                                      fetch->url},
                                     temp_dir());
                             }});
        }

        tasks.push_back({L"Unpacking " + fetch->name + L"…",
                         [fetch, release, zip, unpack, patches_dir,
                          mods_dir](Job& job) -> int {
                             std::error_code ec;
                             fs::remove_all(unpack, ec);
                             fs::create_directories(unpack, ec);
                             std::wstring error;
                             if (!unpack_archive(zip, unpack, job, &error)) {
                                 job.log(L"[launcher] " + error + L"\n");
                                 return 1;
                             }
                             if (fetch->patch) {
                                 fs::path found;
                                 const std::wstring delta =
                                     fetch->file.empty() ? L"vanilla.xdelta" : fetch->file;
                                 if (!find_file_named(unpack, delta, &found)) {
                                     job.log(format(L"[launcher] %ls is not in the archive\n",
                                                    delta.c_str()));
                                     return 1;
                                 }
                                 std::wstring version =
                                     release->version.empty() ? fetch->version
                                                              : release->version;
                                 if (version.empty()) version = L"unknown";
                                 fs::create_directories(patches_dir, ec);
                                 const fs::path target =
                                     patches_dir /
                                     (fetch->name + L"-" + version + L".xdelta");
                                 fs::copy_file(found, target,
                                               fs::copy_options::overwrite_existing, ec);
                                 if (ec) {
                                     job.log(format(L"[launcher] cannot write %ls: %ls\n",
                                                    target.wstring().c_str(),
                                                    widen(ec.message()).c_str()));
                                     return 1;
                                 }
                                 job.log(format(L"[launcher] patch: %ls\n",
                                                target.filename().wstring().c_str()));
                                 return 0;
                             }
                             fs::path source =
                                 fetch->folder.empty() ? unpack : unpack / fetch->folder;
                             const fs::path found = find_mod_root(source, fetch->id);
                             if (found.empty()) {
                                 job.log(format(L"[launcher] %ls holds no mod.txt\n",
                                                fetch->name.c_str()));
                                 return 1;
                             }
                             const fs::path target = mods_dir / fetch->id;
                             fs::remove_all(target, ec);
                             fs::create_directories(target.parent_path(), ec);
                             fs::copy(found, target, fs::copy_options::recursive, ec);
                             if (ec) {
                                 job.log(format(L"[launcher] cannot write %ls: %ls\n",
                                                target.wstring().c_str(),
                                                widen(ec.message()).c_str()));
                                 return 1;
                             }
                             return 0;
                         }});
    }

    host_.start_job(std::move(tasks), {.quiet = true, .done = [this](int code) {
        if (code == 0) {
            refresh();
            apply_now();
        } else if (code == 130) {
            note_ = L"The download was cancelled; nothing was changed.";
            host_.invalidate();
        } else {
            note_ = L"The mods could not be downloaded.";
            host_.invalidate();
        }
    }});
}

// Checks the chosen patch, then writes what the panel has checked. The check decodes
// windows of a delta that can be hundreds of megabytes, so it runs in the launcher's job
// where Activity can say what is happening and Cancel stops it: a synchronous check would
// leave the window frozen with no sign of work.
void ModsScreen::apply_now() {
    if (!model_.project()) return;

    const ModChoice* chosen_patch = selection_.pending_patch().empty() ? nullptr : selection_.find(selection_.pending_patch());
    if (!chosen_patch || !chosen_patch->installed) {
        commit({}, {});
        return;
    }

    const fs::path delta = chosen_patch->delta;
    // A delta this disc already carries a manifest for was checked when it was first
    // applied: decoding a few hundred megabytes again on every Apply buys nothing, since
    // the runtime trusts the same manifest without re-verifying it.
    if (const auto known = read_patch_fit(model_.project()->disc(), delta)) {
        patch_note_ = L"The patch was checked against this disc when it was applied.";
        commit(delta, *known);
        return;
    }
    const fs::path disc = model_.project()->disc();
    auto fit = std::make_shared<PatchFit>();
    std::vector<Task> tasks;
    tasks.push_back({L"Checking " + delta.filename().wstring() + L" against the disc…",
                     [delta, disc, fit](Job& job) -> int {
                         *fit = check_patch(delta, disc);
                         if (!fit->opened) {
                             job.log(format(L"[launcher] %ls\n", fit->error.c_str()));
                             return 1;
                         }
                         return 0;
                     }});
    host_.start_job(std::move(tasks),
              {.quiet = true, .done = [this, delta, fit](int code) {
        if (code == 130) {
            note_ = L"The check was cancelled; nothing was changed.";
            host_.invalidate();
            return;
        }
        if (code != 0) {
            patch_note_ = fit->error.empty() ? L"That patch cannot be used." : fit->error;
            host_.invalidate();
            return;
        }
        if (fit->sampled && fit->matched == 0) {
            // Nothing matched, so this is a delta from some other disc: applying it would
            // boot a game whose files are the wrong ones, which reads as a crash.
            patch_note_ = L"That patch is not made from this disc.";
            host_.log(L"[launcher] patch: " + patch_note_ + L"\n");
            host_.invalidate();
            return;
        }
        patch_note_ = fit->matched == fit->sampled
                          ? L"The patch is made from this disc."
                          : format(L"The patch fits this disc: %d of %d pieces checked match.",
                                   fit->matched, fit->sampled);
        commit(delta, *fit);
    }});
}

// Writes what the panel has checked: the patch manifest, the mods' overlay and the
// settings that remember the choice. The patch has already been checked against the disc.
void ModsScreen::commit(const fs::path& delta, const PatchFit& fit) {
    if (!model_.project()) return;

    if (delta.empty()) {
        if (write_patch_manifest(model_.project()->disc(), {}, {}))
            host_.log(L"[launcher] the disc is played as it is again\n");
        model_.settings().patches.erase(model_.project()->game());
        patch_delta_.clear();
        patch_fit_ = {};
        patch_note_ = L"None: the disc as it is.";
    } else {
        write_patch_manifest(model_.project()->disc(), delta, fit);
        model_.settings().patches[model_.project()->game()] = delta.wstring();
        patch_delta_ = delta;
        patch_fit_ = fit;
        host_.log(format(L"[launcher] patch: %ls\n", delta.filename().c_str()));
    }

    selection_.commit();
    model_.settings().mods[model_.project()->game()] = selection_.enabled();
    model_.settings().save();

    // Activity says what the overlay became, once; refresh() reads the disk back so the
    // table shows the same state the runtime will.
    write_overlay();
    note_ = L"Applied. The game reads this the next time it starts.";
    host_.refresh();
    selection_.take_from_written();
    host_.invalidate();
}

// The runtime serves reads of the extracted disc by offset, so a mod is a list of the
// extents its files cover, written beside disc.manifest and read at boot. The disc itself
// is never changed: switching a mod off deletes the list, it does not extract again.
void ModsScreen::write_overlay() {
    if (!model_.project() || host_.rendering()) return;
    std::vector<ModInfo> chosen;
    for (const auto& id : selection_.enabled()) {
        const ModChoice* entry = selection_.find(id);
        if (!entry || entry->patch || !entry->installed) continue;
        chosen.push_back(entry->mod);
    }
    auto sync = sync_overlay_manifest(model_.project()->disc(), model_.project()->manifest(), chosen);
    auto programs = sync_mod_programs(model_.project()->disc(), chosen, geckos_.enabled_entries());
    // Refresh runs often, so Activity only hears about a change: a mod switched on or off,
    // or one that cannot be served and why.
    std::wstring summary;
    for (const auto& warning : sync.warnings)
        summary += L"[launcher] mod: " + warning + L"\n";
    for (const auto& warning : programs.warnings)
        summary += L"[launcher] mod: " + warning + L"\n";
    if (!chosen.empty())
        summary = format(L"[launcher] mods: %zu file(s) served in place of the disc's\n",
                         sync.files) +
                  summary;
    if (programs.codes) {
        summary += format(L"[launcher] mods: %zu code(s), %zu operations a frame",
                          programs.codes, programs.ops);
        if (programs.words)
            summary += format(L", and %zu instruction word(s) built into the game",
                              programs.words);
        summary += L"\n";
    }
    if (summary == log_) return;
    const bool turned_off = summary.empty() && !log_.empty();
    log_ = summary;
    if (summary.empty()) {
        if (turned_off) host_.log(L"[launcher] mods: none\n");
        return;
    }
    host_.log(summary);
}

// Opens a folder the panel points at, saying so when it is not there or cannot be opened.
void ModsScreen::open_folder(const fs::path& path) {
    if (path.empty()) return;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        host_.message(L"Not there yet", L"Nothing at:\n\n" + path.wstring());
        return;
    }
    std::wstring error;
    if (!open_path(path, &error)) {
        host_.message(L"Cannot open", error.empty()
                                          ? L"This system could not open:\n\n" + path.wstring()
                                          : error);
    }
}

bool ModsScreen::handles(Id id) const {
    if (id >= Id::ModRow && id < Id::ModRowEnd) return true;
    return id == Id::ModsFolder || id == Id::ModChoose || id == Id::ModApply;
}

void ModsScreen::activate(Id id) {
    if (id >= Id::ModRow && id < Id::ModRowEnd) {
        const int row = static_cast<int>(id) - static_cast<int>(Id::ModRow);
        if (row >= 0 && row < static_cast<int>(selection_.rows())) {
            toggle(selection_.entries()[static_cast<std::size_t>(row)].id);
        }
        return;
    }
    switch (id) {
        case Id::ModsFolder:
            if (model_.project()) open_folder(model_.project()->mods_dir());
            host_.invalidate();
            return;
        case Id::ModChoose:
            choose_patch();
            return;
        case Id::ModApply:
            apply();
            return;
        default:
            return;
    }
}

void ModsScreen::paint_table(const PaintContext& ctx, const Rect& panel, bool busy) {
        // One row per mod this disc allows: a folder in mods/, or a disc patch. The checks
        // are the choice Apply writes; a known mod whose files are not here yet is
        // downloaded when it is checked.
        const float left = panel.x + 26;
        const float right = panel.x + panel.w - 26;
        const float version_right = right - 150;
        const float version_left = version_right - 90;
        float y = panel.y + 60;
        ctx.painter.text(L"Only mods this disc can play are listed. A disc patch replaces the "
                      L"whole disc. Apply writes the checks.",
                      RectF(left, y, right, y + 20), Face::Tiny, rgb(theme::Muted));
        y += 26;
        ctx.painter.text(L"On", RectF(left, y, left + 24, y + 16), Face::Tiny,
                      rgb(theme::Muted));
        ctx.painter.text(L"Mod", RectF(left + 30, y, version_left - 6, y + 16), Face::Tiny,
                      rgb(theme::Muted));
        ctx.painter.text(L"Version", RectF(version_left, y, version_right - 6, y + 16),
                      Face::Tiny, rgb(theme::Muted), TextAlign::Trailing);
        ctx.painter.text(L"Author", RectF(version_right, y, right, y + 16), Face::Tiny,
                      rgb(theme::Muted), TextAlign::Trailing);
        y += 22;
        for (size_t i = 0; i < selection_.rows(); ++i) {
            const ModChoice& entry = selection_.entries()[i];
            Id id = static_cast<Id>(static_cast<int>(Id::ModRow) + static_cast<int>(i));
            const bool available = selection_.downloadable(entry.id);
            std::wstring label = entry.name;
            if (entry.patch) label += L" · disc patch";
            else if (!entry.mod.gecko_files.empty()) label += L" · codes";
            if (!entry.installed) label += L" · download";
            ctx.ui.checkbox(id, left, y + 6, ctx.ui.elide(label, Face::Small, version_left - left - 30),
                     selection_.checked(entry.id), !busy && available);
            ctx.painter.text(ctx.ui.elide(entry.version, Face::Tiny, 84),
                          RectF(version_left, y + 4, version_right - 6, y + 26), Face::Tiny,
                          rgb(theme::Muted), TextAlign::Trailing);
            ctx.painter.text(ctx.ui.elide(entry.author, Face::Tiny, right - version_right),
                          RectF(version_right, y + 4, right, y + 26), Face::Tiny,
                          rgb(theme::Muted), TextAlign::Trailing);
            // The whole row is the checkbox, so a click anywhere on it is the same choice.
            if (available && !busy) ctx.ui.register_hit(id, Rect{left - 4, y - 4, right - left + 8, 30});
            y += 34;
        }

        // What the row under the cursor is, or what the panel will do when Apply is
        // pressed. The description is here rather than in a column because it is the one
        // thing about a mod a table cannot keep short.
        const ModChoice* hovered = nullptr;
        for (size_t i = 0; i < selection_.rows(); ++i) {
            if (ctx.ui.hover() == static_cast<Id>(static_cast<int>(Id::ModRow) + static_cast<int>(i)))
                hovered = &selection_.entries()[i];
        }
        const bool staged =
            selection_.staged();
        std::wstring note;
        if (hovered && !hovered->description.empty()) {
            note = hovered->description;
        } else if (!note_.empty()) {
            note = note_;
        } else if (staged) {
            note = L"Apply to write these changes to the game.";
        } else {
            note = summary();
        }
        ctx.painter.text(ctx.ui.elide(note, Face::Small, right - left),
                      RectF(left, y + 8, right, y + 30), Face::Small, rgb(theme::Muted));
        if (!patch_note_.empty()) {
            ctx.painter.text(ctx.ui.elide(patch_note_, Face::Tiny, right - left),
                          RectF(left, y + 30, right, y + 48), Face::Tiny, rgb(theme::Muted));
        } else if (selection_.rows() < selection_.entries().size()) {
            ctx.painter.text(format(L"… and %zu more in the mods folder.",
                                 selection_.entries().size() - selection_.rows()),
                          RectF(left, y + 30, right, y + 48), Face::Tiny, rgb(theme::Muted));
        }
        ctx.ui.link(Id::ModsFolder, Rect{left, panel.y + panel.h - 84, 160, 18},
             L"Open the mods folder");
        ctx.ui.link(Id::ModChoose, Rect{left + 172, panel.y + panel.h - 84, 190, 18},
             L"Choose a patch file…");
        ctx.ui.button(Id::ModApply,
               Rect{panel.x + panel.w - 26 - 130 - 8 - 110, panel.y + panel.h - 56, 110, 36},
               L"Apply", true, !busy && staged);
}

}  // namespace nla
