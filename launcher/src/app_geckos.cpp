// The Geckos panel: the codes a game can be played with, chosen one at a time, and the box
// a block pasted from an editor goes into.
//
// A mod's codes come and go with the mod. These do not: each is switched on by itself, kept
// in geckos/<DISCID>.txt beside the games, and written into the same mods.gecko/mods.code
// the mods write, so the runtime and the recompiler need to know nothing about them.

#include "app.h"
#include "app_shared.h"

#include "codes.h"
#include "util.h"

#include <algorithm>

namespace nla {

void App::refresh_geckos() {
    code_entries_.clear();
    enabled_codes_.clear();
    online_label_.clear();

    // Stand-ins for the renders: the same list and the same picks wherever this is checked,
    // so the drawing does not depend on a machine's disk or network.
    if (rendering_) {
        code_entries_.push_back(
            {L"Infinite Time [Ralf]", L"040374B8 60000000\n", false, true, 1, {}});
        code_entries_.push_back(
            {L"Unlock Everything [Nick]", L"02223258 0017FFFF\n042232E8 0001FF03\n", false,
             true, 2, {}});
        code_entries_.push_back({L"Skip Three Intro Videos [Nick]",
                                 L"0400CB14 60000000\n0400CB28 60000000\n0400CB3C 60000000\n",
                                 false, true, 3, {}});
        code_entries_.push_back(
            {L"Default Round Time [Ralf]", L"042232E8 0001FF03\n", true, true, 1, {}});
        // The one the panel has to show as present but unusable: a code this recompiler
        // cannot run, because it inserts instructions where a recompiled game has none.
        code_entries_.push_back(
            {L"Enable DPad During Fights [Ralf]",
             L"C200E51C 00000002\n80030000 540007BD\n60000000 00000000\n", true, false, 0,
             L"an inserted assembly block needs a PowerPC core, which a recompiled game has "
             L"not at run time"});
        enabled_codes_.push_back(L"Infinite Time [Ralf]");
        online_label_ = L"GNT4";
        return;
    }

    if (!project_) return;
    const std::wstring disc_id = project_->disc_id();
    code_entries_ = read_codes(project_->root(), disc_id);

    CodeSource source;
    const bool has_guide = online_code_source(disc_id, &source);
    for (const auto& entry : code_entries_) {
        if (!entry.online) continue;
        online_label_ = source.label;
        break;
    }

    // The guide's codes are fetched without being asked for: a player opening the panel for
    // a disc that has a guide should see its codes, not an empty table and a link they have
    // to find. It happens here rather than on the click that opens the panel, so the fetch is
    // over by the time the panel is open, and a disc whose list is already on the machine
    // asks for nothing.
    if (has_guide && !codes_fetch_tried_) {
        std::error_code ec;
        const fs::path cache = online_code_file(project_->root(), disc_id);
        if (!fs::is_regular_file(cache, ec)) {
            codes_fetch_tried_ = true;
            fetch_online_codes();
        }
    }

    // What settings remembers, in the order it was switched on so a later code wins where
    // two write the same address.
    auto saved = settings_.codes.find(project_->game());
    if (saved == settings_.codes.end()) return;
    for (const auto& name : saved->second) {
        for (const auto& entry : code_entries_) {
            if (entry.name != name) continue;
            enabled_codes_.push_back(name);
            break;
        }
    }
}

std::size_t App::code_rows() const {
    return std::min<std::size_t>(code_rows_fit_, code_entries_.size());
}

const CodeEntry* App::find_code(const std::wstring& name) const {
    for (const auto& entry : code_entries_) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

bool App::code_checked(const std::wstring& name) const {
    return std::find(pending_codes_.begin(), pending_codes_.end(), name) != pending_codes_.end();
}

void App::open_geckos() {
    // The panel has no button for the guide's list, because it is fetched on its own. Opening
    // it is then how a player asks for another try at one that could not be fetched.
    if (codes_fetch_failed_) {
        codes_fetch_tried_ = false;
        codes_fetch_failed_ = false;
    }
    refresh_geckos();
    pending_codes_ = enabled_codes_;
    codes_note_.clear();
    gecko_edit_.clear();
    gecko_editing_ = false;
    code_offset_ = 0;
    window_.set_text_input(false);
    if (code_entries_.empty() && !rendering_) {
        CodeSource source;
        if (!project_) {
            codes_note_ = L"Set up the game folder first: the codes are kept beside it.";
        } else if (!online_code_source(project_->disc_id(), &source)) {
            // Saying so outright matters: without a guide there is nothing for Refresh list
            // to fetch, and a bare "no codes yet" would send a player looking for one.
            codes_note_ = format(L"No code guide is known for %ls. Paste a code below to add "
                                 L"one.",
                                 project_->disc_id().c_str());
        } else {
            codes_note_ = L"No codes for this disc yet. Paste one below, or press Refresh "
                          L"list.";
        }
    }
    overlay_.show_panel(OverlayPage::Geckos,
                        project_ ? L"Geckos — " + project_->title() : L"Geckos");
    settings_open_ = false;
    scale_open_ = false;
    device_map_.set_menu_row(-1);

    invalidate();
}

void App::toggle_code(const std::wstring& name) {
    const CodeEntry* entry = find_code(name);
    if (!entry) return;
    // A code this recompiler cannot run would be a check that changes nothing, so it is
    // listed to be read and not switched on, the way a mod with no files cannot be.
    if (!entry->runnable) {
        codes_note_ = format(L"%ls needs instructions this game cannot run, so it cannot be "
                            L"switched on.",
                            name.c_str());
        invalidate();
        return;
    }
    auto found = std::find(pending_codes_.begin(), pending_codes_.end(), name);
    if (found == pending_codes_.end()) {
        pending_codes_.push_back(name);
    } else {
        pending_codes_.erase(found);
    }
    codes_note_.clear();
    invalidate();
}

std::vector<CodeEntry> App::enabled_code_entries() const {
    std::vector<CodeEntry> chosen;
    if (!project_ || rendering_ || enabled_codes_.empty()) return chosen;
    const auto mine = read_codes(project_->root(), project_->disc_id());
    for (const auto& name : enabled_codes_) {
        for (const auto& entry : mine) {
            if (entry.name != name) continue;
            chosen.push_back(entry);
            break;
        }
    }
    return chosen;
}

void App::apply_geckos() {
    if (!project_ || busy_) return;
    if (rendering_) {
        // A render has no disk to write: it checks that the click reaches the button.
        codes_note_ = L"Applied in a render check only.";
        invalidate();
        return;
    }

    // What was pasted joins this disc's own file, a code of the same name replaced rather
    // than added again: pasting a code back in is how it is edited.
    std::vector<std::wstring> warnings;
    auto pasted = parse_pasted_codes(gecko_edit_, &warnings);
    if (!pasted.empty()) {
        const fs::path path = code_file(project_->root(), project_->disc_id());
        auto mine = read_code_file(path, false);
        for (const auto& code : pasted) {
            bool replaced = false;
            for (auto& own : mine) {
                if (own.name != code.name) continue;
                own.text = code.text;
                replaced = true;
                break;
            }
            if (!replaced) mine.push_back(code);
            if (!code_checked(code.name)) pending_codes_.push_back(code.name);
        }
        if (!write_code_file(path, mine)) {
            codes_note_ = L"The codes could not be written to geckos/.";
            invalidate();
            return;
        }
        append_log(format(L"[launcher] geckos: %zu code(s) added from the box\n",
                          pasted.size()));
        gecko_edit_.clear();
    }

    enabled_codes_ = pending_codes_;
    if (enabled_codes_.empty()) {
        settings_.codes.erase(project_->game());
    } else {
        settings_.codes[project_->game()] = enabled_codes_;
    }
    save_settings();

    for (const auto& warning : warnings) append_log(L"[launcher] gecko: " + warning + L"\n");

    // refresh() reads the disk back and writes the program, so the panel shows the state
    // the runtime will.
    refresh();
    pending_codes_ = enabled_codes_;

    if (!pasted.empty()) {
        codes_note_ = format(L"Added %zu code(s). Applied: the game reads this the next time "
                            L"it starts.",
                            pasted.size());
    } else if (!warnings.empty()) {
        codes_note_ = warnings.front();
    } else {
        codes_note_ = L"Applied. The game reads this the next time it starts.";
    }
    gecko_editing_ = false;
    window_.set_text_input(false);
    invalidate();
}

void App::fetch_online_codes() {
    // A render has no network to ask and nothing to keep an answer in.
    if (!project_ || rendering_ || codes_fetching_) return;

    const std::wstring disc_id = project_->disc_id();
    CodeSource source;
    if (!online_code_source(disc_id, &source)) {
        codes_note_ = disc_id.empty()
                          ? std::wstring(L"This game has no disc yet, so there is nothing to "
                                         L"look up.")
                          : format(L"No code guide is known for %ls.", disc_id.c_str());
        invalidate();
        return;
    }
    const std::wstring curl = curl_tool();
    if (curl.empty()) {
        codes_note_ = L"curl was not found, and the launcher uses it to reach the guide.";
        invalidate();
        return;
    }

    const fs::path cache = online_code_file(project_->root(), disc_id);
    std::error_code ec;
    fs::create_directories(cache.parent_path(), ec);
    if (ec) {
        codes_note_ = L"geckos/ could not be written, so the list cannot be kept.";
        invalidate();
        return;
    }
    // curl writes beside the target and the file is moved into place, so a failed or
    // cut-off download leaves no half list to read.
    fs::path partial = cache;
    partial += L".part";

    if (codes_thread_.joinable()) codes_thread_.join();
    codes_fetching_ = true;
    codes_note_ = L"Fetching the codes for this disc\u2026";
    append_log(format(L"[launcher] geckos: asking %ls for the codes\n", source.label.c_str()));
    invalidate();

    codes_thread_ = std::thread([this, curl, source, cache, partial] {
        int status = -1;
        capture_output(curl,
                       {L"-fsSL", L"--max-time", L"25", L"--connect-timeout", L"8", L"-o",
                        partial.wstring(), source.url},
                       nullptr, &status);
        std::error_code ec;
        bool fetched = status == 0 && fs::is_regular_file(partial, ec);
        std::size_t found = 0;
        if (fetched) {
            // Whatever shape the guide came in, it is written out as `$Named` blocks, so the
            // panel reads every source the same way.
            const auto codes = parse_guide(read_text_file(partial), source.format);
            found = codes.size();
            fetched = found > 0 && write_code_file(cache, codes);
        }
        // The download is only the source: what is kept is the codes read out of it, so the
        // partial file goes whether the read worked or not.
        fs::remove(partial, ec);

        post([this, fetched, found, source] {
            codes_fetching_ = false;
            if (codes_stop_) return;
            if (!fetched) {
                codes_fetch_failed_ = true;
                codes_note_ =
                    format(L"%ls gave no codes this launcher could read. Press Refresh "
                           L"list to try again.",
                           source.label.c_str());
                append_log(L"[launcher] geckos: the guide could not be fetched\n");
                invalidate();
                return;
            }
            codes_fetch_failed_ = false;
            refresh_geckos();
            codes_note_ = format(L"%zu code(s) from %ls.", found, source.label.c_str());
            append_log(format(L"[launcher] geckos: %zu code(s) from %ls\n", found,
                              source.label.c_str()));
            invalidate();
        });
    });
}

}  // namespace nla
