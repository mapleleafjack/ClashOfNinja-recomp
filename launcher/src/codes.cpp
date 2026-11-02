// The codes a game can be played with, apart from mods: one list per disc, switched on one
// code at a time, and the community guide that fills it.
//
// A mod carries its own codes, and switching the mod on switches all of them on. A code
// here is separate from that: it is chosen on its own, kept in geckos/<DISCID>.txt beside
// the games, and reaches the game by the same road a mod's codes do -- mods.gecko, which the
// runtime runs, and mods.code, which the recompiler turns into native instructions.

#include "codes.h"

#include "gecko.h"
#include "util.h"

#include <algorithm>

namespace nla {
namespace {

// The guides that hold these discs' codes. Keyed by disc ID, because a code's addresses are
// one build's own: the European and American releases of a game are different builds, and a
// code written for one must never be offered for the other.
struct Guide {
    const wchar_t* disc_id;
    const wchar_t* url;
    const wchar_t* label;
    CodeFormat format;
};

// Where each disc's codes are written down. Two kinds of place: the markdown guides the
// GNT modding repo keeps, and Ralf's gc-forever threads, which are the only write-up of the
// two Clash of Ninja releases on the GameCube. GNT3 has neither, and says so rather than
// borrowing another build's codes -- a code's addresses are one disc's own.
constexpr Guide kGuides[] = {
    {L"G4NJDA",
     L"https://raw.githubusercontent.com/NicholasMoser/Naruto-GNT-Modding/main/gnt4/docs/"
     L"guides/gecko_codes.md",
     L"GNT4", CodeFormat::Markdown},
    {L"GNUEDA",
     L"https://raw.githubusercontent.com/NicholasMoser/Naruto-GNT-Modding/main/con2/docs/"
     L"guides/gecko_codes.md",
     L"Clash of Ninja 2", CodeFormat::Markdown},
    {L"GNREDA", L"https://www.gc-forever.com/forums/viewtopic.php?t=2176",
     L"gc-forever", CodeFormat::ForumPost},
    {L"GNUPDA", L"https://www.gc-forever.com/forums/viewtopic.php?t=2175",
     L"gc-forever", CodeFormat::ForumPost},
};

// A code is worth listing when it has a word line; `parse_named_codes` already refuses a
// block with none, so this is only about what the recompiler can make of it.
std::size_t count_operations(const std::string& ops) {
    std::size_t count = 0;
    std::size_t start = 0;
    while (start < ops.size()) {
        auto end = ops.find('\n', start);
        if (end == std::string::npos) end = ops.size();
        if (end > start && ops[start] != '#') ++count;
        start = end + 1;
    }
    return count;
}

// Fills in what the parser made of a code: how many operations a frame runs, and whether
// there is anything at all for this game to do.
void describe(CodeEntry* entry) {
    auto program = parse_gecko(entry->text, entry->name);
    entry->ops = count_operations(program.ops);
    entry->runnable = entry->ops > 0 || !program.words.empty();
    entry->warning = program.warnings.empty() ? std::wstring() : program.warnings.front();
}

// Two codes with one name would be one check in the panel and one entry in settings.
std::wstring unique_name(const std::vector<CodeEntry>& codes, const std::wstring& wanted) {
    std::wstring unique = wanted;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const auto& code : codes) {
            if (code.name == unique) {
                taken = true;
                break;
            }
        }
        if (!taken) return unique;
        unique = format(L"%ls (%d)", wanted.c_str(), n);
    }
}

}  // namespace

fs::path codes_dir(const fs::path& root) { return root / L"geckos"; }

fs::path code_file(const fs::path& root, const std::wstring& disc_id) {
    return codes_dir(root) / (disc_id + L".txt");
}

fs::path online_code_file(const fs::path& root, const std::wstring& disc_id) {
    // Kept in the same format as a player's own codes: whatever a guide is fetched as, it is
    // written out as `$Named` blocks, so reading it back needs no knowledge of where it came
    // from.
    return codes_dir(root) / L"online" / (disc_id + L".txt");
}

std::vector<CodeEntry> read_code_file(const fs::path& path, bool online) {
    std::vector<CodeEntry> codes;
    if (path.empty()) return codes;
    const auto text = read_text_file(path);
    if (text.empty()) return codes;
    (void)online;
    for (const auto& named : parse_named_codes(text, L"Code")) {
        CodeEntry entry;
        entry.name = named.name;
        entry.text = named.text;
        entry.online = online;
        describe(&entry);
        codes.push_back(std::move(entry));
    }
    return codes;
}

bool write_code_file(const fs::path& path, const std::vector<CodeEntry>& codes) {
    if (codes.empty()) {
        std::error_code ec;
        fs::remove(path, ec);
        return !ec;
    }
    std::wstring text;
    for (const auto& code : codes) {
        text += L"$" + code.name + L"\n";
        text += code.text;
        if (text.empty() || text.back() != L'\n') text += L'\n';
        text += L"$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n";
    }
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    return write_text_file(path, narrow(text));
}

std::vector<CodeEntry> read_codes(const fs::path& root, const std::wstring& disc_id) {
    auto codes = read_code_file(code_file(root, disc_id), false);
    for (auto& entry : read_code_file(online_code_file(root, disc_id), true)) {
        bool shadowed = false;
        for (const auto& own : codes) {
            if (own.name == entry.name) {
                shadowed = true;
                break;
            }
        }
        if (!shadowed) codes.push_back(std::move(entry));
    }
    return codes;
}

std::vector<CodeEntry> parse_pasted_codes(const std::wstring& text,
                                          std::vector<std::wstring>* warnings) {
    std::vector<CodeEntry> codes;
    for (const auto& named : parse_named_codes(text, L"Pasted code")) {
        auto program = parse_gecko(named.text, named.name);
        if (warnings) {
            for (const auto& warning : program.warnings) warnings->push_back(warning);
        }
        if (program.ops.empty() && program.words.empty()) {
            // Nothing would happen in the game, so adding it would only look like it did
            // something. Saying so is the whole point of checking on Apply.
            if (warnings) {
                warnings->push_back(format(L"%ls: nothing this game can run, so it was not added",
                                           named.name.c_str()));
            }
            continue;
        }
        CodeEntry entry;
        entry.name = unique_name(codes, named.name);
        entry.text = named.text;
        codes.push_back(std::move(entry));
    }
    return codes;
}

bool online_code_source(const std::wstring& disc_id, CodeSource* source) {
    const auto wanted = to_lower(disc_id);
    for (const auto& guide : kGuides) {
        if (to_lower(guide.disc_id) != wanted) continue;
        if (source) {
            source->url = guide.url;
            source->label = guide.label;
            source->format = guide.format;
        }
        return true;
    }
    return false;
}

// A line that is only a word and a value, `XXXXXXXX YYYYYYYY`, as every guide writes them.
bool is_word_line(const std::wstring& line) {
    const auto parts = split(line, L' ');
    std::vector<std::wstring> words;
    for (const auto& part : parts) {
        if (!part.empty()) words.push_back(part);
    }
    if (words.size() != 2) return false;
    for (const auto& word : words) {
        if (word.size() != 8) return false;
        if (word.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) return false;
    }
    return true;
}

// The text of every `<pre><code>` block in a page: where a forum keeps the codes, so the
// markup around them is dropped before they are read.
std::wstring code_boxes(const std::wstring& page) {
    std::wstring text;
    std::size_t at = 0;
    for (;;) {
        const auto open = page.find(L"<code>", at);
        if (open == std::wstring::npos) break;
        const auto close = page.find(L"</code>", open);
        if (close == std::wstring::npos) break;
        auto block = page.substr(open + 6, close - open - 6);
        // Tags inside a code block are only ever the forum's own highlighting, and the few
        // entities that matter are the ones a `<` or `&` in a name would have become.
        std::wstring plain;
        plain.reserve(block.size());
        bool in_tag = false;
        for (wchar_t c : block) {
            if (c == L'<') {
                in_tag = true;
                continue;
            }
            if (c == L'>') {
                in_tag = false;
                continue;
            }
            if (!in_tag) plain.push_back(c);
        }
        for (const auto& [from, to] : {std::pair<std::wstring, std::wstring>{L"&amp;", L"&"},
                                       {L"&lt;", L"<"},
                                       {L"&gt;", L">"},
                                       {L"&quot;", L"\""},
                                       {L"&#039;", L"'"},
                                       {L"&nbsp;", L" "}}) {
            for (std::size_t found = plain.find(from); found != std::wstring::npos;
                 found = plain.find(from, found + to.size())) {
                plain.replace(found, from.size(), to);
            }
        }
        text += plain;
        text += L'\n';
        at = close + 7;
    }
    return text;
}

std::vector<CodeEntry> parse_plain_codes(const std::wstring& text) {
    std::vector<CodeEntry> codes;
    std::wstring name;
    std::wstring body;

    auto flush = [&codes, &name, &body] {
        if (!name.empty() && !body.empty()) {
            CodeEntry entry;
            entry.name = unique_name(codes, name);
            entry.text = body;
            entry.online = true;
            describe(&entry);
            codes.push_back(std::move(entry));
        }
        name.clear();
        body.clear();
    };

    for (const auto& raw : split(text, L'\n')) {
        const auto line = trim(raw);
        if (line.empty()) continue;
        if (is_word_line(line)) {
            // Words with no name above them belong to nothing a player could switch on.
            if (name.empty()) continue;
            body += line;
            body += L'\n';
            continue;
        }
        // Prose between codes is read as a name and then dropped, because nothing follows
        // it: a guide's note about a code is not itself a code.
        flush();
        name = line;
    }
    flush();
    return codes;
}

std::vector<CodeEntry> parse_guide(const std::wstring& text, CodeFormat format) {
    if (format == CodeFormat::ForumPost) return parse_plain_codes(code_boxes(text));
    return parse_online_codes(text);
}

std::vector<CodeEntry> parse_online_codes(const std::wstring& markdown) {
    std::vector<CodeEntry> codes;
    std::wstring heading;
    std::wstring body;
    bool in_fence = false;
    bool taking = false;

    auto add = [&codes](const std::wstring& name, const std::wstring& text) {
        if (name.empty()) return;
        CodeEntry entry;
        entry.name = unique_name(codes, name);
        entry.text = text;
        entry.online = true;
        describe(&entry);
        codes.push_back(std::move(entry));
    };

    for (const auto& raw : split(markdown, L'\n')) {
        const auto line = trim(raw);
        if (line.rfind(L"```", 0) == 0) {
            if (!in_fence) {
                in_fence = true;
                // A guide tags its code blocks `gecko`; an untagged one under a heading is
                // read as the same thing, since that is where the words are.
                const auto tag = trim(line.substr(3));
                taking = !heading.empty() &&
                         (tag.empty() || tag == L"gecko" || tag == L"asm" || tag == L"c" ||
                          tag == L"C");
                body.clear();
            } else {
                in_fence = false;
                if (taking) {
                    add(heading, body);
                    // One block per heading: a second fence under the same name is a note,
                    // not another code.
                    heading.clear();
                }
                taking = false;
            }
            continue;
        }
        if (in_fence) {
            if (taking) {
                body += raw;
                body += L'\n';
            }
            continue;
        }
        // Both depths name a code: a guide with subsections puts its codes under `###`, and
        // one with a flat list puts them straight under `##`. A `# ` line is the title, so
        // it ends whatever was being read rather than starting anything.
        if (line.rfind(L"### ", 0) == 0) {
            heading = trim(line.substr(4));
            continue;
        }
        if (line.rfind(L"## ", 0) == 0) {
            heading = trim(line.substr(3));
            continue;
        }
        if (line.rfind(L"# ", 0) == 0) heading.clear();
    }
    return codes;
}

void CodeSelection::clear() {
    entries_.clear();
    enabled_.clear();
    pending_.clear();
}

void CodeSelection::take_from_written() { pending_ = enabled_; }

void CodeSelection::commit() { enabled_ = pending_; }

const CodeEntry* CodeSelection::find(const std::wstring& name) const {
    for (const auto& entry : entries_) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

bool CodeSelection::checked(const std::wstring& name) const {
    return std::find(pending_.begin(), pending_.end(), name) != pending_.end();
}

bool CodeSelection::runnable(const std::wstring& name) const {
    const CodeEntry* entry = find(name);
    return entry && entry->runnable;
}

std::size_t CodeSelection::rows(std::size_t fit) const {
    return std::min(fit, entries_.size());
}

bool CodeSelection::toggle(const std::wstring& name) {
    const CodeEntry* entry = find(name);
    if (!entry || !entry->runnable) return false;
    auto found = std::find(pending_.begin(), pending_.end(), name);
    if (found == pending_.end()) {
        pending_.push_back(name);
    } else {
        pending_.erase(found);
    }
    return true;
}

}  // namespace nla
