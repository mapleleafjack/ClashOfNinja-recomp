#pragma once

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace nla {

namespace fs = std::filesystem;

// A Gecko code the player can switch on for a game: one they pasted into the launcher, or
// one of the codes the community's guide for the disc holds. `text` is the code's words and
// notes, without the `$Name` line that names it.
struct CodeEntry {
    std::wstring name;
    std::wstring text;
    bool online = false;  // from the guide for this disc, not written here
    // What the parser could make of it. A code this recompiler cannot run is still listed,
    // so a player can see it exists and read why it does nothing here.
    bool runnable = true;
    // How many operations it becomes, once a frame.
    std::size_t ops = 0;
    // Why a code cannot run here, in the parser's own words, so the panel can say it.
    std::wstring warning;
};

// What the Geckos panel offers, and what is checked on it.
//
// `pending` is the check the panel is showing and `enabled` is what has been written; Apply
// is the step between them. The order is the order they were switched on, so a later code
// wins at a shared address.
class CodeSelection {
  public:
    std::vector<CodeEntry>& entries() { return entries_; }
    const std::vector<CodeEntry>& entries() const { return entries_; }
    std::vector<std::wstring>& enabled() { return enabled_; }
    const std::vector<std::wstring>& enabled() const { return enabled_; }
    std::vector<std::wstring>& pending() { return pending_; }
    const std::vector<std::wstring>& pending() const { return pending_; }

    void clear();
    // The panel is opened: it shows what is written, so nothing reads as changed.
    void take_from_written();
    // Apply wrote it, so what was staged is now what the game reads.
    void commit();
    bool staged() const { return pending_ != enabled_; }

    const CodeEntry* find(const std::wstring& name) const;
    bool checked(const std::wstring& name) const;
    // A code this recompiler cannot run is listed to be read, not switched on: it would be a
    // check that changes nothing. The panel says why, in its own note.
    bool runnable(const std::wstring& name) const;
    // The rows the panel draws, of the ones that fit.
    std::size_t rows(std::size_t fit) const;
    // Flips a code's check. False when the code cannot be switched on here.
    bool toggle(const std::wstring& name);

  private:
    std::vector<CodeEntry> entries_;
    std::vector<std::wstring> enabled_;
    std::vector<std::wstring> pending_;
};

// Where a disc's codes live: geckos/<DISCID>.txt beside the games, and the guide's answer
// cached beside it. Keyed by disc ID because a code's addresses are one build's own, and a
// code written for another build must never be offered.
fs::path codes_dir(const fs::path& root);
fs::path code_file(const fs::path& root, const std::wstring& disc_id);
fs::path online_code_file(const fs::path& root, const std::wstring& disc_id);

// The codes a file holds, each `$Name` block separately. Empty when there is no file.
std::vector<CodeEntry> read_code_file(const fs::path& path, bool online);
// Writes them back as an editor would, one `$Name` block each. False when it could not be
// written.
bool write_code_file(const fs::path& path, const std::vector<CodeEntry>& codes);

// Every code the launcher offers for a disc: the player's own, then the guide's. A name in
// both is the player's, so a code they edited by pasting it again is the one that runs.
std::vector<CodeEntry> read_codes(const fs::path& root, const std::wstring& disc_id);

// The codes in a block pasted into the launcher. A block with nothing this game can run is
// not added; `warnings` says which and why, for the panel and the Activity log.
std::vector<CodeEntry> parse_pasted_codes(const std::wstring& text,
                                          std::vector<std::wstring>* warnings);

// How a guide writes its codes: two shapes cover every source this launcher knows.
enum class CodeFormat {
    Markdown,   // a `## Name` or `### Name` heading, with a fenced block under it
    ForumPost,  // a gc-forever thread: a name on its own line, its words under it
};

// The community's per-game guide, which is where these discs' codes are written down.
struct CodeSource {
    std::wstring url;
    std::wstring label;
    CodeFormat format = CodeFormat::Markdown;
};

// False when no guide is known for `disc_id`: the panel then says there are none rather
// than offering codes written for another build's addresses.
bool online_code_source(const std::wstring& disc_id, CodeSource* source);

// The codes in a guide, in whichever of the two shapes it is written.
std::vector<CodeEntry> parse_guide(const std::wstring& text, CodeFormat format);

// The codes in a markdown guide: each `## Name` or `### Name` and the block under it.
std::vector<CodeEntry> parse_online_codes(const std::wstring& markdown);

// The codes in a plain list: a name on its own line, its words under it. This is what a
// forum post's code box holds, and what an editor's export looks like without the `$`s.
std::vector<CodeEntry> parse_plain_codes(const std::wstring& text);

}  // namespace nla
