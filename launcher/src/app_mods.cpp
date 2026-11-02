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
    mod_entries_.clear();
    enabled_mods_.clear();
    applied_patch_id_.clear();
    catalog_mods_.clear();

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
        mod_entries_.push_back(std::move(patch));

        ModChoice first;
        first.id = L"retuned-opening";
        first.name = L"Retuned Opening";
        first.version = L"1.0";
        first.author = L"example";
        first.description = L"Swaps the banner in the opening movie.";
        first.disc_ids = {L"g4njda"};
        first.installed = true;
        first.known = true;
        mod_entries_.push_back(std::move(first));

        ModChoice second;
        second.id = L"alt-colours";
        second.name = L"Alternate Colours";
        second.version = L"0.9";
        second.author = L"example";
        second.description = L"Alternate costumes for the whole roster.";
        second.disc_ids = {L"g4njda"};
        second.installed = true;
        second.known = true;
        mod_entries_.push_back(std::move(second));

        enabled_mods_.push_back(L"retuned-opening");
        applied_patch_id_ = L"scon4";
        patch_delta_ = L"SCON4-2.1.0.xdelta";
        return;
    }

    if (!project_) return;
    const std::wstring disc_id = project_->disc_id();
    const fs::path patches_dir = patch_directory(project_->root());

    // What the tree carries is the applied patch; a manifest naming a file that is gone,
    // as a download into a temp folder is, falls back to what settings remembers.
    refresh_patch();

    std::wstring catalog_error;
    catalog_mods_ = load_mod_catalog(mod_catalog_path(), &catalog_error);
    if (catalog_error != catalog_log_) {
        catalog_log_ = catalog_error;
        if (!catalog_error.empty()) append_log(L"[launcher] " + catalog_error + L"\n");
    }

    // The table: the catalog's entries for this disc, the folders beside the games, and the
    // deltas in patches/, merged by id.
    mod_entries_ = collect_mods(catalog_mods_, project_->mods_dir(), patches_dir, disc_id);

    // What is written now, in the order it was switched on so a later mod wins where two
    // replace the same file.
    auto saved = settings_.mods.find(project_->game());
    if (saved != settings_.mods.end()) {
        for (const auto& id : saved->second) {
            for (const auto& entry : mod_entries_) {
                if (entry.patch || entry.id != id) continue;
                enabled_mods_.push_back(id);
                break;
            }
        }
    }

    if (patch_delta_.empty()) return;
    for (const auto& entry : mod_entries_) {
        if (!entry.patch || !same_path(entry.delta, patch_delta_)) continue;
        applied_patch_id_ = entry.id;
        return;
    }
    // A patch chosen earlier and kept elsewhere is still a row, so the table can show what
    // is applied and switch it off.
    ModChoice local;
    local.id = unique_mod_id(mod_entries_, L"patch:" + patch_delta_.filename().wstring());
    local.name = patch_delta_.stem().wstring();
    local.patch = true;
    local.installed = true;
    local.delta = patch_delta_;
    applied_patch_id_ = local.id;
    mod_entries_.push_back(std::move(local));
}

const ModChoice* App::find_mod(const std::wstring& id) const {
    for (const auto& entry : mod_entries_) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

// What the panel draws in a row's checkbox. The selection is staged: it is what Apply will
// write, not what the game is playing until then.
bool App::mod_checked(const std::wstring& id) const {
    const ModChoice* entry = find_mod(id);
    if (entry && entry->patch) return pending_patch_ == id;
    return std::find(pending_mods_.begin(), pending_mods_.end(), id) != pending_mods_.end();
}

bool App::mod_downloadable(const std::wstring& id) const {
    const ModChoice* entry = find_mod(id);
    if (!entry) return false;
    if (entry->installed) return true;
    return entry->known && entry->catalog < catalog_mods_.size() &&
           catalog_mods_[entry->catalog].downloadable();
}

std::wstring App::mods_summary() const {
    if (rendering_) return L"2 of 3 on.";
    if (!project_) return L"Set up the game folder first.";
    if (mod_entries_.empty()) return L"No mods for this disc.";
    const std::size_t on = enabled_mods_.size() + (applied_patch_id_.empty() ? 0 : 1);
    auto text = format(L"%zu of %zu on.", on, mod_entries_.size());
    if (status_.stale_code) text += L" A code patch needs a build.";
    return text;
}

std::size_t App::mod_rows() const {
    // Enough rows to fill the panel on the smallest window the launcher opens, and no
    // more: a folder with a dozen mods says how many are past the end instead of running
    // off the page.
    constexpr std::size_t kMaxRows = 10;
    return std::min(mod_entries_.size(), kMaxRows);
}

void App::toggle_mod(const std::wstring& id) {
    const ModChoice* entry = find_mod(id);
    if (!entry) return;
    // A row whose files are neither here nor downloadable cannot be applied, so it is not
    // a choice the panel offers.
    if (!mod_downloadable(id)) return;
    if (entry->patch) {
        pending_patch_ = pending_patch_ == id ? std::wstring() : id;
    } else {
        auto found = std::find(pending_mods_.begin(), pending_mods_.end(), id);
        if (found == pending_mods_.end()) {
            pending_mods_.push_back(id);
        } else {
            pending_mods_.erase(found);
        }
    }
    mods_note_.clear();
    invalidate();
}

void App::open_mods() {
    refresh_mods();
    pending_mods_ = enabled_mods_;
    pending_patch_ = applied_patch_id_;
    mods_note_.clear();
    if (mod_entries_.empty() && !rendering_) {
        mods_note_ =
            project_ ? L"No mods for this disc. A mod is a folder in mods/ with a mod.txt "
                       L"naming this disc's ID and a disc/ tree of the files it replaces."
                     : L"Set up the game folder first: the mods are kept beside it.";
    }
    overlay_open_ = true;
    overlay_mods_ = true;
    overlay_players_ = false;
    overlay_geckos_ = false;
    overlay_confirm_ = false;
    overlay_update_ = false;
    overlay_callback_ = nullptr;
    overlay_title_ = project_ ? L"Mods — " + project_->title() : L"Mods";
    overlay_action_ = L"Done";
    settings_open_ = false;
    scale_open_ = false;
    pad_menu_row_ = -1;
    invalidate();
}

// A disc patch replaces the whole disc the game reads, so it is one choice at a time: the
// checked patch row, kept wherever the player keeps the delta, and decoded by the runtime
// as the game asks for its files.
void App::refresh_patch() {
    patch_delta_.clear();
    patch_fit_ = {};
    if (!project_ || rendering_) return;
    std::error_code ec;
    patch_delta_ = read_patch_delta(project_->disc());
    // A manifest can name a delta a download made in a temp folder, which is not a patch
    // the player still has. Only a file that is here counts as the applied patch.
    if (!patch_delta_.empty() && !fs::is_regular_file(patch_delta_, ec)) patch_delta_.clear();
    if (patch_delta_.empty()) {
        // Settings remembers what was chosen, so a tree that was extracted again gets its
        // patch back without asking for the file a second time.
        auto saved = settings_.patches.find(project_->game());
        if (saved != settings_.patches.end() && !saved->second.empty() &&
            fs::is_regular_file(fs::path(saved->second), ec)) {
            patch_delta_ = saved->second;
        }
    }
}

void App::choose_patch() {
    if (!project_) return;
    fs::path start = patch_delta_.empty() ? project_->disc() : patch_delta_;
    choose_path(PickerKind::Patch, L"Choose the patch to play with", start,
                [this](const fs::path& chosen) { stage_patch(chosen); });
}

// A delta from this machine joins the table checked, and Apply writes it. A patch is not
// applied on the click that chooses it: the panel has one Apply for everything it lists.
void App::stage_patch(const fs::path& delta) {
    if (!project_ || delta.empty()) return;
    for (auto& entry : mod_entries_) {
        if (entry.patch && same_path(entry.delta, delta)) {
            pending_patch_ = entry.id;
            invalidate();
            return;
        }
    }
    ModChoice entry;
    entry.id = unique_mod_id(mod_entries_, L"patch:" + delta.filename().wstring());
    entry.name = delta.stem().wstring();
    entry.patch = true;
    entry.installed = true;
    entry.delta = delta;
    mod_entries_.push_back(std::move(entry));
    pending_patch_ = mod_entries_.back().id;
    mods_note_.clear();
    invalidate();
}

// Applies the checks the panel shows: first the mods this machine does not have are
// fetched, then the overlay and the patch are written. Nothing is written before every
// chosen download is here, so a failed download leaves the game as it was.
void App::apply_mods() {
    if (!project_ || busy_) return;
    if (rendering_) {
        // A render has no disk to write: it checks that the click reaches the button.
        mods_note_ = L"Applied in a render check only.";
        invalidate();
        return;
    }

    std::vector<std::wstring> missing;
    for (const auto& id : pending_mods_) {
        const ModChoice* entry = find_mod(id);
        if (!entry || entry->installed) continue;
        if (entry->known && entry->catalog < catalog_mods_.size() &&
            catalog_mods_[entry->catalog].downloadable()) {
            missing.push_back(id);
        }
    }
    if (!pending_patch_.empty()) {
        const ModChoice* entry = find_mod(pending_patch_);
        if (entry && !entry->installed && entry->known &&
            entry->catalog < catalog_mods_.size() &&
            catalog_mods_[entry->catalog].downloadable()) {
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
    if (!project_) return;
    const fs::path patches_dir = patch_directory(project_->root());
    const fs::path mods_dir = project_->mods_dir();

    std::vector<Task> tasks;
    for (const auto& id : ids) {
        const ModChoice* entry = find_mod(id);
        if (!entry || entry->catalog >= catalog_mods_.size()) continue;
        const CatalogMod& known = catalog_mods_[entry->catalog];
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

    job_quiet_ = true;
    start_job(std::move(tasks), false, false, [this](int code) {
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
    });
}

// Checks the chosen patch, then writes what the panel has checked. The check decodes
// windows of a delta that can be hundreds of megabytes, so it runs in the launcher's job
// where Activity can say what is happening and Cancel stops it: a synchronous check would
// leave the window frozen with no sign of work.
void App::apply_mods_now() {
    if (!project_) return;

    const ModChoice* chosen_patch = pending_patch_.empty() ? nullptr : find_mod(pending_patch_);
    if (!chosen_patch || !chosen_patch->installed) {
        commit_mods({}, {});
        return;
    }

    const fs::path delta = chosen_patch->delta;
    // A delta this disc already carries a manifest for was checked when it was first
    // applied: decoding a few hundred megabytes again on every Apply buys nothing, since
    // the runtime trusts the same manifest without re-verifying it.
    if (const auto known = read_patch_fit(project_->disc(), delta)) {
        patch_note_ = L"The patch was checked against this disc when it was applied.";
        commit_mods(delta, *known);
        return;
    }
    const fs::path disc = project_->disc();
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
    job_quiet_ = true;
    start_job(std::move(tasks), false, false, [this, delta, fit](int code) {
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
    });
}

// Writes what the panel has checked: the patch manifest, the mods' overlay and the
// settings that remember the choice. The patch has already been checked against the disc.
void App::commit_mods(const fs::path& delta, const PatchFit& fit) {
    if (!project_) return;

    if (delta.empty()) {
        if (write_patch_manifest(project_->disc(), {}, {}))
            append_log(L"[launcher] the disc is played as it is again\n");
        settings_.patches.erase(project_->game());
        patch_delta_.clear();
        patch_fit_ = {};
        patch_note_ = L"None: the disc as it is.";
    } else {
        write_patch_manifest(project_->disc(), delta, fit);
        settings_.patches[project_->game()] = delta.wstring();
        patch_delta_ = delta;
        patch_fit_ = fit;
        append_log(format(L"[launcher] patch: %ls\n", delta.filename().c_str()));
    }

    enabled_mods_ = pending_mods_;
    applied_patch_id_ = pending_patch_;
    settings_.mods[project_->game()] = enabled_mods_;
    save_settings();

    // Activity says what the overlay became, once; refresh() reads the disk back so the
    // table shows the same state the runtime will.
    write_mod_overlay();
    mods_note_ = L"Applied. The game reads this the next time it starts.";
    refresh();
    pending_mods_ = enabled_mods_;
    pending_patch_ = applied_patch_id_;
    invalidate();
}

// The runtime serves reads of the extracted disc by offset, so a mod is a list of the
// extents its files cover, written beside disc.manifest and read at boot. The disc itself
// is never changed: switching a mod off deletes the list, it does not extract again.
void App::write_mod_overlay() {
    if (!project_ || rendering_) return;
    std::vector<ModInfo> chosen;
    for (const auto& id : enabled_mods_) {
        const ModChoice* entry = find_mod(id);
        if (!entry || entry->patch || !entry->installed) continue;
        chosen.push_back(entry->mod);
    }
    auto sync = sync_overlay_manifest(project_->disc(), project_->manifest(), chosen);
    auto programs = sync_mod_programs(project_->disc(), chosen, enabled_code_entries());
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
