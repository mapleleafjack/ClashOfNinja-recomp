// The Geckos panel: the codes a game can be played with, chosen one at a time, and the box
// a block pasted from an editor goes into.

#include "geckos_screen.h"

#include "tools.h"
#include "util.h"

#include <algorithm>

namespace nla {

void GeckosScreen::shutdown() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

void GeckosScreen::refresh() {
    codes_.clear();
    online_label_.clear();

    // Stand-ins for the renders: the same list and the same picks wherever this is checked,
    // so the drawing does not depend on a machine's disk or network.
    if (host_.rendering()) {
        codes_.entries().push_back(
            {L"Infinite Time [Ralf]", L"040374B8 60000000\n", false, true, 1, {}});
        codes_.entries().push_back(
            {L"Unlock Everything [Nick]", L"02223258 0017FFFF\n042232E8 0001FF03\n", false,
             true, 2, {}});
        codes_.entries().push_back({L"Skip Three Intro Videos [Nick]",
                                 L"0400CB14 60000000\n0400CB28 60000000\n0400CB3C 60000000\n",
                                 false, true, 3, {}});
        codes_.entries().push_back(
            {L"Default Round Time [Ralf]", L"042232E8 0001FF03\n", true, true, 1, {}});
        // The one the panel has to show as present but unusable: a code this recompiler
        // cannot run, because it inserts instructions where a recompiled game has none.
        codes_.entries().push_back(
            {L"Enable DPad During Fights [Ralf]",
             L"C200E51C 00000002\n80030000 540007BD\n60000000 00000000\n", true, false, 0,
             L"an inserted assembly block needs a PowerPC core, which a recompiled game has "
             L"not at run time"});
        codes_.enabled().push_back(L"Infinite Time [Ralf]");
        online_label_ = L"GNT4";
        return;
    }

    if (!host_.project()) return;
    const std::wstring disc_id = host_.project()->disc_id();
    codes_.entries() = read_codes(host_.project()->root(), disc_id);

    CodeSource source;
    const bool has_guide = online_code_source(disc_id, &source);
    for (const auto& entry : codes_.entries()) {
        if (!entry.online) continue;
        online_label_ = source.label;
        break;
    }

    // The guide's codes are fetched without being asked for: a player opening the panel for
    // a disc that has a guide should see its codes, not an empty table and a link they have
    // to find. It happens here rather than on the click that opens the panel, so the fetch is
    // over by the time the panel is open, and a disc whose list is already on the machine
    // asks for nothing.
    if (has_guide && !fetch_tried_) {
        std::error_code ec;
        const fs::path cache = online_code_file(host_.project()->root(), disc_id);
        if (!fs::is_regular_file(cache, ec)) {
            fetch_tried_ = true;
            fetch_online();
        }
    }

    // What settings remembers, in the order it was switched on so a later code wins where
    // two write the same address.
    auto saved = host_.settings().codes.find(host_.project()->game());
    if (saved == host_.settings().codes.end()) return;
    for (const auto& name : saved->second) {
        for (const auto& entry : codes_.entries()) {
            if (entry.name != name) continue;
            codes_.enabled().push_back(name);
            break;
        }
    }
}

void GeckosScreen::describe_empty() {
    if (!codes_.entries().empty() || host_.rendering()) return;
    CodeSource source;
    if (!host_.project()) {
        note_ = L"Set up the game folder first: the codes are kept beside it.";
    } else if (!online_code_source(host_.project()->disc_id(), &source)) {
        // Saying so outright matters: without a guide there is nothing for Refresh list to
        // fetch, and a bare "no codes yet" would send a player looking for one.
        note_ = format(L"No code guide is known for %ls. Paste a code below to add one.",
                       host_.project()->disc_id().c_str());
    } else {
        note_ = L"No codes for this disc yet. Paste one below, or press Refresh list.";
    }
}

void GeckosScreen::open() {
    // The panel has no button for the guide's list, because it is fetched on its own. Opening
    // it is then how a player asks for another try at one that could not be fetched.
    if (fetch_failed_) {
        fetch_tried_ = false;
        fetch_failed_ = false;
    }
    refresh();
    codes_.take_from_written();
    note_.clear();
    edit_.clear();
    editing_ = false;
    offset_ = 0;
    host_.text_input(false);
    describe_empty();
    host_.show_panel(OverlayPage::Geckos,
                     host_.project() ? L"Geckos — " + host_.project()->title() : L"Geckos");
    host_.invalidate();
}

void GeckosScreen::toggle(const std::wstring& name) {
    // A code this recompiler cannot run is listed to be read, not switched on: it would be a
    // check that changes nothing, the way a mod with no files cannot be applied.
    if (!codes_.toggle(name)) {
        note_ = format(L"%ls needs instructions this game cannot run, so it cannot be "
                       L"switched on.",
                       name.c_str());
        host_.invalidate();
        return;
    }
    note_.clear();
    host_.invalidate();
}

std::vector<CodeEntry> GeckosScreen::enabled_entries() const {
    std::vector<CodeEntry> chosen;
    if (!host_.project() || host_.rendering() || codes_.enabled().empty()) return chosen;
    const auto mine = read_codes(host_.project()->root(), host_.project()->disc_id());
    for (const auto& name : codes_.enabled()) {
        for (const auto& entry : mine) {
            if (entry.name != name) continue;
            chosen.push_back(entry);
            break;
        }
    }
    return chosen;
}

void GeckosScreen::apply() {
    if (!host_.project() || host_.busy()) return;
    if (host_.rendering()) {
        // A render has no disk to write: it checks that the click reaches the button.
        note_ = L"Applied in a render check only.";
        host_.invalidate();
        return;
    }

    // What was pasted joins this disc's own file, a code of the same name replaced rather
    // than added again: pasting a code back in is how it is edited.
    std::vector<std::wstring> warnings;
    auto pasted = parse_pasted_codes(edit_, &warnings);
    if (!pasted.empty()) {
        const fs::path path = code_file(host_.project()->root(), host_.project()->disc_id());
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
            if (!codes_.checked(code.name)) codes_.pending().push_back(code.name);
        }
        if (!write_code_file(path, mine)) {
            note_ = L"The codes could not be written to geckos/.";
            host_.invalidate();
            return;
        }
        host_.log(format(L"[launcher] geckos: %zu code(s) added from the box\n",
                         pasted.size()));
        edit_.clear();
    }

    codes_.commit();
    if (codes_.enabled().empty()) {
        host_.settings().codes.erase(host_.project()->game());
    } else {
        host_.settings().codes[host_.project()->game()] = codes_.enabled();
    }
    host_.settings().save();

    for (const auto& warning : warnings) host_.log(L"[launcher] gecko: " + warning + L"\n");

    // The re-sync reads the disk back and writes the program, so the panel shows the state
    // the runtime will.
    host_.refresh();
    codes_.take_from_written();

    if (!pasted.empty()) {
        note_ = format(L"Added %zu code(s). Applied: the game reads this the next time "
                       L"it starts.",
                       pasted.size());
    } else if (!warnings.empty()) {
        note_ = warnings.front();
    } else {
        note_ = L"Applied. The game reads this the next time it starts.";
    }
    stop_editing();
}

void GeckosScreen::fetch_online() {
    // A render has no network to ask and nothing to keep an answer in.
    if (!host_.project() || host_.rendering() || fetching_) return;

    const std::wstring disc_id = host_.project()->disc_id();
    CodeSource source;
    if (!online_code_source(disc_id, &source)) {
        note_ = disc_id.empty()
                    ? std::wstring(L"This game has no disc yet, so there is nothing to "
                                   L"look up.")
                    : format(L"No code guide is known for %ls.", disc_id.c_str());
        host_.invalidate();
        return;
    }
    const std::wstring curl = curl_tool();
    if (curl.empty()) {
        note_ = L"curl was not found, and the launcher uses it to reach the guide.";
        host_.invalidate();
        return;
    }

    const fs::path cache = online_code_file(host_.project()->root(), disc_id);
    std::error_code ec;
    fs::create_directories(cache.parent_path(), ec);
    if (ec) {
        note_ = L"geckos/ could not be written, so the list cannot be kept.";
        host_.invalidate();
        return;
    }
    // curl writes beside the target and the file is moved into place, so a failed or
    // cut-off download leaves no half list to read.
    fs::path partial = cache;
    partial += L".part";

    if (thread_.joinable()) thread_.join();
    stop_ = false;
    fetching_ = true;
    note_ = L"Fetching the codes for this disc\u2026";
    host_.log(format(L"[launcher] geckos: asking %ls for the codes\n", source.label.c_str()));
    host_.invalidate();

    thread_ = std::thread([this, curl, source, cache, partial] {
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

        host_.post([this, fetched, found, source] {
            fetching_ = false;
            if (stop_) return;
            if (!fetched) {
                fetch_failed_ = true;
                note_ = format(L"%ls gave no codes this launcher could read. Press Refresh "
                               L"list to try again.",
                               source.label.c_str());
                host_.log(L"[launcher] geckos: the guide could not be fetched\n");
                host_.invalidate();
                return;
            }
            fetch_failed_ = false;
            refresh();
            note_ = format(L"%zu code(s) from %ls.", found, source.label.c_str());
            host_.log(format(L"[launcher] geckos: %zu code(s) from %ls\n", found,
                             source.label.c_str()));
            host_.invalidate();
        });
    });
}

void GeckosScreen::stop_editing() {
    editing_ = false;
    host_.text_input(false);
    host_.invalidate();
}

bool GeckosScreen::key(ScreenKey key) {
    switch (key) {
        case ScreenKey::Escape:
            // The box leaves first, before the panel it sits in closes.
            stop_editing();
            return true;
        case ScreenKey::Enter:
            edit_ += L'\n';
            host_.invalidate();
            return true;
        case ScreenKey::Backspace:
            if (!edit_.empty()) {
                edit_.pop_back();
                // A UTF-16 surrogate pair must not be split.
                if (sizeof(wchar_t) == 2 && !edit_.empty() && edit_.back() >= 0xD800 &&
                    edit_.back() <= 0xDBFF) {
                    edit_.pop_back();
                }
                host_.invalidate();
            }
            return true;
        case ScreenKey::Paste: {
            auto text = host_.clipboard();
            if (!text.empty()) {
                edit_ += text;
                host_.invalidate();
            }
            return true;
        }
        case ScreenKey::Tab:
        case ScreenKey::Up:
        case ScreenKey::Down:
            return false;
    }
    return false;
}

bool GeckosScreen::text(const std::wstring& typed) {
    edit_ += typed;
    host_.invalidate();
    return true;
}

void GeckosScreen::scroll(int notches) {
    const std::size_t rows = codes_.rows(rows_fit_);
    const std::size_t most =
        codes_.entries().size() > rows ? codes_.entries().size() - rows : 0;
    const long wanted = static_cast<long>(offset_) - static_cast<long>(notches) * 3;
    offset_ = static_cast<std::size_t>(
        std::max(0L, std::min(static_cast<long>(most), wanted)));
    host_.invalidate();
}

void GeckosScreen::clamp_offset() {
    const std::size_t rows = codes_.rows(rows_fit_);
    if (rows > 0 && offset_ + rows > codes_.entries().size())
        offset_ = codes_.entries().size() - rows;
    if (rows == 0) offset_ = 0;
}

bool GeckosScreen::handles(Id id) const {
    if (id >= Id::CodeRow && id < Id::CodeRowEnd) return true;
    return id == Id::CodeBox || id == Id::CodeApply || id == Id::CodeRefresh;
}

void GeckosScreen::activate(Id id) {
    if (id >= Id::CodeRow && id < Id::CodeRowEnd) {
        const int row = static_cast<int>(id) - static_cast<int>(Id::CodeRow);
        if (row >= 0 && row < static_cast<int>(codes_.rows(rows_fit_))) {
            toggle(codes_.entries()[offset_ + static_cast<std::size_t>(row)].name);
        }
        return;
    }
    switch (id) {
        case Id::CodeBox:
            // The box holds what is typed at the end of it; a block pasted in is the way a
            // code gets here, so the field is the same one the picker uses.
            editing_ = true;
            host_.text_input(true);
            host_.invalidate();
            return;
        case Id::CodeApply:
            apply();
            return;
        case Id::CodeRefresh:
            fetch_online();
            return;
        default:
            return;
    }
}

}  // namespace nla
