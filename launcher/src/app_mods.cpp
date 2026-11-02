// The mods a game's disc allows, the patch it is played with, and the overlay the
// runtime reads.

#include "app.h"

#include "catalog.h"
#include "patches.h"

#include "util.h"

#include <algorithm>

namespace nla {

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
void App::refresh_mods() {
    mods_.clear();

    // Stand-ins for the renders: the same table and the same picks wherever this is
    // checked, so the drawing does not depend on a machine's disk or network.
    if (rendering_) {
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
        mods_.entries().push_back(std::move(patch));

        ModChoice first;
        first.id = L"retuned-opening";
        first.name = L"Retuned Opening";
        first.version = L"1.0";
        first.author = L"example";
        first.description = L"Swaps the banner in the opening movie.";
        first.disc_ids = {L"g4njda"};
        first.installed = true;
        first.known = true;
        mods_.entries().push_back(std::move(first));

        ModChoice second;
        second.id = L"alt-colours";
        second.name = L"Alternate Colours";
        second.version = L"0.9";
        second.author = L"example";
        second.description = L"Alternate costumes for the whole roster.";
        second.disc_ids = {L"g4njda"};
        second.installed = true;
        second.known = true;
        mods_.entries().push_back(std::move(second));

        mods_.enabled().push_back(L"retuned-opening");
        mods_.applied_patch() = L"scon4";
        patch_delta_ = L"SCON4-2.1.0.xdelta";
        return;
    }

    if (!state_.project()) return;
    const std::wstring disc_id = state_.project()->disc_id();
    const fs::path patches_dir = patch_directory(state_.project()->root());

    // What the tree carries is the applied patch; a manifest naming a file that is gone,
    // as a download into a temp folder is, falls back to what settings remembers.
    refresh_patch();

    std::wstring catalog_error;
    mods_.catalog() = load_mod_catalog(mod_catalog_path(), &catalog_error);
    if (catalog_error != catalog_log_) {
        catalog_log_ = catalog_error;
        if (!catalog_error.empty()) append_log(L"[launcher] " + catalog_error + L"\n");
    }

    // The table: the catalog's entries for this disc, the folders beside the games, and the
    // deltas in patches/, merged by id.
    mods_.entries() = collect_mods(mods_.catalog(), state_.project()->mods_dir(), patches_dir, disc_id);

    // What is written now, in the order it was switched on so a later mod wins where two
    // replace the same file.
    auto saved = state_.settings().mods.find(state_.project()->game());
    if (saved != state_.settings().mods.end()) {
        for (const auto& id : saved->second) {
            for (const auto& entry : mods_.entries()) {
                if (entry.patch || entry.id != id) continue;
                mods_.enabled().push_back(id);
                break;
            }
        }
    }

    if (patch_delta_.empty()) return;
    for (const auto& entry : mods_.entries()) {
        if (!entry.patch || !same_path(entry.delta, patch_delta_)) continue;
        mods_.applied_patch() = entry.id;
        return;
    }
    // A patch chosen earlier and kept elsewhere is still a row, so the table can show what
    // is applied and switch it off.
    ModChoice local;
    local.id = unique_mod_id(mods_.entries(), L"patch:" + patch_delta_.filename().wstring());
    local.name = patch_delta_.stem().wstring();
    local.patch = true;
    local.installed = true;
    local.delta = patch_delta_;
    mods_.applied_patch() = local.id;
    mods_.entries().push_back(std::move(local));
}

std::wstring App::mods_summary() const {
    if (rendering_) return L"2 of 3 on.";
    if (!state_.project()) return L"Set up the game folder first.";
    if (mods_.entries().empty()) return L"No mods for this disc.";
    const std::size_t on = mods_.enabled().size() + (mods_.applied_patch().empty() ? 0 : 1);
    auto text = format(L"%zu of %zu on.", on, mods_.entries().size());
    if (state_.status().stale_code) text += L" A code patch needs a build.";
    return text;
}

void App::toggle_mod(const std::wstring& id) {
    if (!mods_.downloadable(id)) return;
    mods_.toggle(id);
    mods_note_.clear();
    invalidate();
}

void App::open_mods() {
    refresh_mods();
    mods_.take_from_written();
    mods_note_.clear();
    if (mods_.entries().empty() && !rendering_) {
        mods_note_ =
            state_.project() ? L"No mods for this disc. A mod is a folder in mods/ with a mod.txt "
                       L"naming this disc's ID and a disc/ tree of the files it replaces."
                     : L"Set up the game folder first: the mods are kept beside it.";
    }
    overlay_.show_panel(OverlayPage::Mods,
                        state_.project() ? L"Mods — " + state_.project()->title() : L"Mods");
    close_popups();
    invalidate();
}

// A disc patch replaces the whole disc the game reads, so it is one choice at a time: the
// checked patch row, kept wherever the player keeps the delta, and decoded by the runtime
// as the game asks for its files.
void App::refresh_patch() {
    patch_delta_.clear();
    patch_fit_ = {};
    if (!state_.project() || rendering_) return;
    std::error_code ec;
    patch_delta_ = read_patch_delta(state_.project()->disc());
    // A manifest can name a delta a download made in a temp folder, which is not a patch
    // the player still has. Only a file that is here counts as the applied patch.
    if (!patch_delta_.empty() && !fs::is_regular_file(patch_delta_, ec)) patch_delta_.clear();
    if (patch_delta_.empty()) {
        // Settings remembers what was chosen, so a tree that was extracted again gets its
        // patch back without asking for the file a second time.
        auto saved = state_.settings().patches.find(state_.project()->game());
        if (saved != state_.settings().patches.end() && !saved->second.empty() &&
            fs::is_regular_file(fs::path(saved->second), ec)) {
            patch_delta_ = saved->second;
        }
    }
}

void App::choose_patch() {
    if (!state_.project()) return;
    fs::path start = patch_delta_.empty() ? state_.project()->disc() : patch_delta_;
    picker_.choose_path(PickerKind::Patch, L"Choose the patch to play with", start,
                [this](const fs::path& chosen) { stage_patch(chosen); });
}

// A delta from this machine joins the table checked, and Apply writes it. A patch is not
// applied on the click that chooses it: the panel has one Apply for everything it lists.
void App::stage_patch(const fs::path& delta) {
    if (!state_.project() || delta.empty()) return;
    for (auto& entry : mods_.entries()) {
        if (entry.patch && same_path(entry.delta, delta)) {
            mods_.pending_patch() = entry.id;
            invalidate();
            return;
        }
    }
    ModChoice entry;
    entry.id = unique_mod_id(mods_.entries(), L"patch:" + delta.filename().wstring());
    entry.name = delta.stem().wstring();
    entry.patch = true;
    entry.installed = true;
    entry.delta = delta;
    mods_.entries().push_back(std::move(entry));
    mods_.pending_patch() = mods_.entries().back().id;
    mods_note_.clear();
    invalidate();
}

// Applies the checks the panel shows: first the mods this machine does not have are
// fetched, then the overlay and the patch are written. Nothing is written before every
// chosen download is here, so a failed download leaves the game as it was.
void App::apply_mods() {
    if (!state_.project() || busy_) return;
    if (rendering_) {
        // A render has no disk to write: it checks that the click reaches the button.
        mods_note_ = L"Applied in a render check only.";
        invalidate();
        return;
    }

    std::vector<std::wstring> missing;
    for (const auto& id : mods_.pending()) {
        const ModChoice* entry = mods_.find(id);
        if (!entry || entry->installed) continue;
        if (entry->known && entry->catalog < mods_.catalog().size() &&
            mods_.catalog()[entry->catalog].downloadable()) {
            missing.push_back(id);
        }
    }
    if (!mods_.pending_patch().empty()) {
        const ModChoice* entry = mods_.find(mods_.pending_patch());
        if (entry && !entry->installed && entry->known &&
            entry->catalog < mods_.catalog().size() &&
            mods_.catalog()[entry->catalog].downloadable()) {
            missing.push_back(entry->id);
        }
    }
    if (!missing.empty()) {
        download_mods(missing);
        return;
    }
    apply_mods_now();
}

// Fetches each chosen mod's archive into a temp folder, unpacks it, and puts the mod's
// folder - or the patch's delta - where the panel looks for it. The work runs in the
// launcher's job, so it reports progress and Cancel stops it like any other step.
void App::download_mods(const std::vector<std::wstring>& ids) {
    if (!state_.project()) return;
    const fs::path patches_dir = patch_directory(state_.project()->root());
    const fs::path mods_dir = state_.project()->mods_dir();

    std::vector<Task> tasks;
    for (const auto& id : ids) {
        const ModChoice* entry = mods_.find(id);
        if (!entry || entry->catalog >= mods_.catalog().size()) continue;
        const CatalogMod& known = mods_.catalog()[entry->catalog];
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
                             // bsdtar reads a zip on every system the launcher runs on.
                             const std::wstring tar = tar_tool();
                             if (tar.empty() ||
                                 job.run_process(tar,
                                                 {L"-xf", zip.wstring(), L"-C",
                                                  unpack.wstring()},
                                                 temp_dir()) != 0) {
                                 job.log(L"[launcher] the downloaded archive could not be "
                                         L"unpacked\n");
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

    start_job(std::move(tasks), {.quiet = true, .done = [this](int code) {
        if (code == 0) {
            refresh_mods();
            apply_mods_now();
        } else if (code == 130) {
            mods_note_ = L"The download was cancelled; nothing was changed.";
            invalidate();
        } else {
            mods_note_ = L"The mods could not be downloaded.";
            invalidate();
        }
    }});
}

// Checks the chosen patch, then writes what the panel has checked. The check decodes
// windows of a delta that can be hundreds of megabytes, so it runs in the launcher's job
// where Activity can say what is happening and Cancel stops it: a synchronous check would
// leave the window frozen with no sign of work.
void App::apply_mods_now() {
    if (!state_.project()) return;

    const ModChoice* chosen_patch = mods_.pending_patch().empty() ? nullptr : mods_.find(mods_.pending_patch());
    if (!chosen_patch || !chosen_patch->installed) {
        commit_mods({}, {});
        return;
    }

    const fs::path delta = chosen_patch->delta;
    // A delta this disc already carries a manifest for was checked when it was first
    // applied: decoding a few hundred megabytes again on every Apply buys nothing, since
    // the runtime trusts the same manifest without re-verifying it.
    if (const auto known = read_patch_fit(state_.project()->disc(), delta)) {
        patch_note_ = L"The patch was checked against this disc when it was applied.";
        commit_mods(delta, *known);
        return;
    }
    const fs::path disc = state_.project()->disc();
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
    start_job(std::move(tasks),
              {.quiet = true, .done = [this, delta, fit](int code) {
        if (code == 130) {
            mods_note_ = L"The check was cancelled; nothing was changed.";
            invalidate();
            return;
        }
        if (code != 0) {
            patch_note_ = fit->error.empty() ? L"That patch cannot be used." : fit->error;
            invalidate();
            return;
        }
        if (fit->sampled && fit->matched == 0) {
            // Nothing matched, so this is a delta from some other disc: applying it would
            // boot a game whose files are the wrong ones, which reads as a crash.
            patch_note_ = L"That patch is not made from this disc.";
            append_log(L"[launcher] patch: " + patch_note_ + L"\n");
            invalidate();
            return;
        }
        patch_note_ = fit->matched == fit->sampled
                          ? L"The patch is made from this disc."
                          : format(L"The patch fits this disc: %d of %d pieces checked match.",
                                   fit->matched, fit->sampled);
        commit_mods(delta, *fit);
    }});
}

// Writes what the panel has checked: the patch manifest, the mods' overlay and the
// settings that remember the choice. The patch has already been checked against the disc.
void App::commit_mods(const fs::path& delta, const PatchFit& fit) {
    if (!state_.project()) return;

    if (delta.empty()) {
        if (write_patch_manifest(state_.project()->disc(), {}, {}))
            append_log(L"[launcher] the disc is played as it is again\n");
        state_.settings().patches.erase(state_.project()->game());
        patch_delta_.clear();
        patch_fit_ = {};
        patch_note_ = L"None: the disc as it is.";
    } else {
        write_patch_manifest(state_.project()->disc(), delta, fit);
        state_.settings().patches[state_.project()->game()] = delta.wstring();
        patch_delta_ = delta;
        patch_fit_ = fit;
        append_log(format(L"[launcher] patch: %ls\n", delta.filename().c_str()));
    }

    mods_.commit();
    state_.settings().mods[state_.project()->game()] = mods_.enabled();
    save_settings();

    // Activity says what the overlay became, once; refresh() reads the disk back so the
    // table shows the same state the runtime will.
    write_mod_overlay();
    mods_note_ = L"Applied. The game reads this the next time it starts.";
    refresh();
    mods_.take_from_written();
    invalidate();
}

// The runtime serves reads of the extracted disc by offset, so a mod is a list of the
// extents its files cover, written beside disc.manifest and read at boot. The disc itself
// is never changed: switching a mod off deletes the list, it does not extract again.
void App::write_mod_overlay() {
    if (!state_.project() || rendering_) return;
    std::vector<ModInfo> chosen;
    for (const auto& id : mods_.enabled()) {
        const ModChoice* entry = mods_.find(id);
        if (!entry || entry->patch || !entry->installed) continue;
        chosen.push_back(entry->mod);
    }
    auto sync = sync_overlay_manifest(state_.project()->disc(), state_.project()->manifest(), chosen);
    auto programs = sync_mod_programs(state_.project()->disc(), chosen, geckos_.enabled_entries());
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
    if (summary == mods_log_) return;
    const bool turned_off = summary.empty() && !mods_log_.empty();
    mods_log_ = summary;
    if (summary.empty()) {
        if (turned_off) append_log(L"[launcher] mods: none\n");
        return;
    }
    append_log(summary);
}

}  // namespace nla
