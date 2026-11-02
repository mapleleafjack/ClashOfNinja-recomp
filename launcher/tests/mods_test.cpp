// Checks the mod loader's offline half: reading a mod.txt, which discs it is for, and
// laying its files over an extracted disc's manifest -- the list the runtime reads at
// boot. Run by ctest; it writes into a temporary folder and needs no game.

#include "mods.h"
#include "catalog.h"
#include "util.h"

#include <cstdio>
#include <string>
#include <vector>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

void check_text(const std::wstring& actual, const std::wstring& expected, const char* what) {
    bool same = actual == expected;
    std::printf("%s %s", same ? "ok  " : "FAIL", what);
    if (!same) std::printf(" (got \"%s\", wanted \"%s\")", nla::narrow(actual).c_str(),
                           nla::narrow(expected).c_str());
    std::printf("\n");
    if (!same) ++failures;
}

void write(const fs::path& path, const std::wstring& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    nla::write_text_file(path, nla::narrow(text));
}

void manifests() {
    nla::ModInfo info;
    check(nla::parse_mod_manifest(
              L"# a mod\nname = Retuned Opening\nversion = 1.2\nauthor = someone\n"
              L"disc_id = G4NJDA\ndescription = Swaps the opening movie.\n",
              &info),
          "a manifest is read");
    check_text(info.name, L"Retuned Opening", "the name is read");
    check_text(info.version, L"1.2", "the version is read");
    check_text(info.author, L"someone", "the author is read");
    check_text(info.description, L"Swaps the opening movie.", "the description is read");
    check(info.disc_ids.size() == 1 && nla::to_lower(info.disc_ids[0]) == L"g4njda",
          "the disc ID is read");

    nla::ModInfo several;
    check(nla::parse_mod_manifest(L"name = Two Builds\ndisc_id = G4NJDA, GNREDA\n", &several),
          "a manifest for several discs is read");
    check(several.disc_ids.size() == 2, "both disc IDs are kept");
    check(nla::mod_supports(several, L"GNREDA"), "the second disc is supported");
    check(!nla::mod_supports(several, L"GNUEDA"), "a disc it does not name is not supported");

    nla::ModInfo empty;
    check(!nla::parse_mod_manifest(L"# nothing here\n", &empty),
          "a manifest with no name and no disc is not a mod");
}

void discovery(const fs::path& root) {
    write(root / L"mods" / L"two-builds" / L"mod.txt",
          L"name = Two Builds\ndisc_id = G4NJDA, GNREDA\nversion = 0.1\n"
          L"gecko = codes.txt\n");
    write(root / L"mods" / L"two-builds" / L"disc" / L"files" / L"opening.bnr", L"abc");
    write(root / L"mods" / L"two-builds" / L"disc" / L"sys" / L"main.dol", L"dol");
    write(root / L"mods" / L"two-builds" / L"codes.txt", L"0400CB14 60000000\n");
    write(root / L"mods" / L"empty-folder" / L"notes.txt", L"not a mod");
    std::error_code ec;
    fs::create_directories(root / L"mods" / L"nothing", ec);

    auto mods = nla::discover_mods(root / L"mods");
    check(mods.size() == 1, "only the folder with a manifest is a mod");
    if (mods.empty()) return;
    check_text(mods[0].folder, L"two-builds", "the folder names the mod");
    check(mods[0].files.size() == 2, "the disc tree lists the files it replaces");
    check_text(mods[0].files[0], L"files/opening.bnr", "the paths are '/'-separated");
    check_text(mods[0].files[1], L"sys/main.dol", "the sys files are listed too");
    check(mods[0].gecko_files.size() == 1 && mods[0].gecko_files[0] == L"codes.txt",
          "the manifest names the codes' file");

    // A mod that names no codes file and has a gecko.txt: what a hand-written mod looks
    // like, and it is found without a line in mod.txt.
    write(root / L"mods" / L"tuned" / L"mod.txt", L"name = Tuned\ndisc_id = G4NJDA\n");
    write(root / L"mods" / L"tuned" / L"gecko.txt", L"0400CB14 60000000\n");
    auto tuned = nla::discover_mods(root / L"mods");
    check(tuned.size() == 2, "a mod with a gecko.txt is still a mod");
    for (const auto& mod : tuned) {
        if (mod.folder != L"tuned") continue;
        check(mod.gecko_files.size() == 1 && mod.gecko_files[0] == L"gecko.txt",
              "gecko.txt is read when mod.txt names no codes");
    }
}

void overlays(const fs::path& root) {
    // An extracted disc, cut down to what the loader looks at: the file offsets a mod's
    // files are laid over.
    write(root / L"gnt4" / L"rom" / L"disc" / L"sys" / L"main.dol", L"0123456789");
    write(root / L"gnt4" / L"rom" / L"disc" / L"disc.manifest",
          L"# GameCube disc manifest\n"
          L"disc_id G4NJDA\n"
          L"image_size 1000\n"
          L"extent 00000000 00000440 00000000 sys/boot.bin\n"
          L"extent 00020300 0000000a 00000000 sys/main.dol\n"
          L"extent 00250000 00000010 00000000 files/audio/bgm/m000.trk\n");

    auto disc = nla::read_disc_extents(root / L"gnt4" / L"rom" / L"disc" / L"disc.manifest");
    check(disc.size() == 3, "the disc's extents are read");
    check(disc[1].offset == 0x20300 && disc[1].size == 10, "an extent keeps its offset and size");

    // A mod replacing the DOL and part of a track, and naming a file the disc has not got.
    write(root / L"mods" / L"short-dol" / L"mod.txt", L"name = Short DOL\ndisc_id = G4NJDA\n");
    write(root / L"mods" / L"short-dol" / L"disc" / L"sys" / L"main.dol", L"xx");
    write(root / L"mods" / L"short-dol" / L"disc" / L"files" / L"audio" / L"bgm" / L"m000.trk",
          L"01234567");
    write(root / L"mods" / L"short-dol" / L"disc" / L"files" / L"not-on-the-disc.bin", L"z");

    // A mod whose file is larger than the disc's, which cannot be laid over it.
    write(root / L"mods" / L"long-track" / L"mod.txt", L"name = Long Track\ndisc_id = G4NJDA\n");
    write(root / L"mods" / L"long-track" / L"disc" / L"files" / L"audio" / L"bgm" /
              L"m000.trk",
          L"0123456789abcdefghij");

    auto mods = nla::discover_mods(root / L"mods");
    check(mods.size() == 2, "both mods are found");
    auto build = nla::build_overlays(disc, mods, root / L"gnt4" / L"rom" / L"disc");
    check(build.extents.size() == 2, "the two files on the disc are laid over");
    check(build.warnings.size() == 2, "the file that is not on the disc and the too-large one warn");
    if (build.extents.size() == 2) {
        // The later mod wins the track, but its file does not fit, so the earlier one's
        // copy stays.
        check(build.extents[0].offset == 0x20300, "the DOL overlay keeps the disc's offset");
        check(build.extents[0].size == 10, "the overlay covers the whole extent");
        check(build.extents[0].available == 2, "a shorter file leaves the rest as zeroes");
        check(build.extents[1].offset == 0x250000, "the track overlay keeps its offset too");
        check(build.extents[1].available == 8, "the track's file is the earlier mod's");
        check(build.extents[0].path.find(L"mods") != std::wstring::npos,
              "the path is relative to the disc folder");
    }

    auto text = nla::overlay_manifest_text(build.extents);
    check(text.find("\noverlay 00020300 0000000a 00000002 ") != std::string::npos,
          "the manifest line carries the offset, size and bytes there are");
    // Nothing enabled is no list at all, so the runtime serves the disc untouched.
    check(nla::overlay_manifest_text({}).find("\noverlay ") == std::string::npos,
          "an empty list has no overlays in it");

    // What the runtime reads: written beside disc.manifest, left alone while unchanged,
    // and removed once nothing is laid over the disc.
    auto disc_dir = root / L"gnt4" / L"rom" / L"disc";
    auto sync = nla::sync_overlay_manifest(disc_dir, disc_dir / L"disc.manifest", mods);
    check(sync.wrote && sync.files == 2, "the overlay list is written");
    auto written = nla::read_text_file(disc_dir / L"mods.manifest");
    check(written.find(L"\noverlay 00020300 0000000a 00000002 ") != std::wstring::npos,
          "the file says which extent and how much of the file there is");
    auto again = nla::sync_overlay_manifest(disc_dir, disc_dir / L"disc.manifest", mods);
    check(!again.wrote && again.files == 2, "an unchanged list is not written again");
    auto cleared = nla::sync_overlay_manifest(disc_dir, disc_dir / L"disc.manifest", {});
    check(cleared.wrote && cleared.files == 0, "switching the mods off removes the list");
    check(!fs::exists(disc_dir / L"mods.manifest"), "the file is gone");

    // A build's addresses are its own: a mod that names another disc is refused even when
    // it is handed to this one.
    nla::ModInfo other;
    other.name = L"For Another Build";
    other.folder = L"other";
    other.dir = root / L"mods" / L"other";
    other.disc_ids = {L"GNUEDA"};
    auto refused = nla::sync_overlay_manifest(disc_dir, disc_dir / L"disc.manifest", {other});
    check(refused.files == 0 && !fs::exists(disc_dir / L"mods.manifest"),
          "a mod for another disc is not laid over this one");
    check(!refused.warnings.empty() && refused.warnings[0].find(L"GNUEDA") != std::wstring::npos,
          "and says which disc it is for");
}

// The codes a mod carries: the program the runtime runs, and the words recompilation
// applies because a write into code cannot change code that was already recompiled.
void programs(const fs::path& root) {
    auto disc_dir = root / L"gnt4" / L"rom" / L"disc";
    write(disc_dir / L"disc.manifest", L"disc_id G4NJDA\nimage_size 1000\n");

    write(root / L"mods" / L"default-codes" / L"mod.txt",
          L"name = Default Codes\ndisc_id = G4NJDA\n");
    write(root / L"mods" / L"default-codes" / L"gecko.txt", L"042232E8 0001FF03\n");
    write(root / L"mods" / L"intros" / L"mod.txt",
          L"name = Skip Intros\ndisc_id = G4NJDA\ngecko = codes.txt\n");
    write(root / L"mods" / L"intros" / L"codes.txt",
          L"# skip the intro videos\n"
          L"0400CB14 60000000\n"
          L"C2042C40 00000001\n"
          L"60000000 00000000\n");
    // A later mod writing the same instruction wins, as a later mod's file does.
    write(root / L"mods" / L"later" / L"mod.txt", L"name = Later\ndisc_id = G4NJDA\n");
    write(root / L"mods" / L"later" / L"gecko.txt", L"0400CB14 48000000\n");
    // And one for another build, which must never be read for this one.
    write(root / L"mods" / L"elsewhere" / L"mod.txt", L"name = Elsewhere\ndisc_id = GNUEDA\n");
    write(root / L"mods" / L"elsewhere" / L"gecko.txt", L"042232E8 00FFFFFF\n");

    auto mods = nla::discover_mods(root / L"mods");
    check(mods.size() == 4, "every mod folder is found");
    auto sync = nla::sync_mod_programs(disc_dir, mods);
    check(sync.wrote && sync.codes == 3, "the three mods for this disc carry codes");
    check(sync.ops == 3, "an inserted assembly block is left out of the program");
    check(sync.words == 2, "two instruction words, the repeated address written once");
    check(sync.warnings.size() == 1 && sync.warnings[0].find(L"PowerPC") != std::wstring::npos,
          "the assembly code is reported");

    auto ops = nla::read_text_file(disc_dir / L"mods.gecko");
    check(ops.find(L"write32 8000CB14 60000000 1 -\n") != std::wstring::npos,
          "the program has the write from the mod that names its codes file");
    check(ops.find(L"# Skip Intros") != std::wstring::npos, "each mod's part is named");
    check(ops.find(L"write32 802232E8 0001FF03 1 -\n") != std::wstring::npos,
          "and the one from the mod with a gecko.txt");
    check(ops.find(L"00FFFFFF") == std::wstring::npos,
          "a mod for another disc is not in the program");

    auto code = nla::read_text_file(disc_dir / L"mods.code");
    check(code.find(L"8000CB14 48000000\n") != std::wstring::npos,
          "the later mod's instruction word is the one kept");
    check(code.find(L"8000CB14 60000000") == std::wstring::npos,
          "and the earlier one is dropped");

    auto again = nla::sync_mod_programs(disc_dir, mods);
    check(!again.wrote, "an unchanged program is not written again");
    auto cleared = nla::sync_mod_programs(disc_dir, {});
    check(cleared.wrote && cleared.codes == 0, "switching the mods off removes the files");
    check(!fs::exists(disc_dir / L"mods.gecko") && !fs::exists(disc_dir / L"mods.code"),
          "both are gone");
}

// -- disc patches -----------------------------------------------------------

// The smallest delta the decoder has to accept: one window, no source, a single ADD that
// writes the target out of the delta's own data section.
std::string add_only_delta(const std::string& target) {
    auto varint = [](std::string* out, std::uint64_t value) {
        unsigned char groups[10];
        int count = 0;
        do {
            groups[count++] = static_cast<unsigned char>(value & 0x7F);
            value >>= 7;
        } while (value);
        for (int i = count - 1; i >= 0; --i) {
            unsigned char byte = groups[i];
            if (i) byte |= 0x80;
            out->push_back(static_cast<char>(byte));
        }
    };
    std::uint32_t a = 1, b = 0;
    for (unsigned char c : target) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    const std::uint32_t adler = (b << 16) | a;

    std::string instructions;
    instructions.push_back(static_cast<char>(1));  // ADD, size from the instruction stream
    varint(&instructions, target.size());

    std::string delta("\xD6\xC3\xC4\x00", 4);  // magic, version, no secondary compressor
    delta.push_back(0);                        // no code table of its own
    std::string body;
    varint(&body, target.size());              // target window length
    body.push_back(0);                         // delta indicator: no secondary compressor
    varint(&body, target.size());              // data section
    varint(&body, instructions.size());        // instruction section
    varint(&body, 0);                          // address section
    body.push_back(static_cast<char>(adler >> 24));
    body.push_back(static_cast<char>((adler >> 16) & 0xFF));
    body.push_back(static_cast<char>((adler >> 8) & 0xFF));
    body.push_back(static_cast<char>(adler & 0xFF));
    body += target;
    body += instructions;
    delta.push_back(0x04);                     // the window carries its target's checksum
    varint(&delta, body.size());
    delta += body;
    return delta;
}

void patches(const fs::path& root) {
    const fs::path disc = root / L"disc";
    std::string content;
    for (int i = 0; i < 4096; ++i) content.push_back(static_cast<char>(i * 7 + 3));
    {
        std::error_code ec;
        fs::create_directories(disc, ec);
        nla::write_text_file(disc / L"disc.manifest",
                             "# test disc\nimage_size 2000\nextent 00000000 00001000 00000000 sys/main.dol\n");
        nla::write_text_file(disc / L"sys" / L"main.dol", content);
    }

    // A patch made from this disc: it carries the target's bytes itself, so it decodes
    // whether or not the disc is the one it came from -- but the launcher must still read
    // it and agree that it fits.
    const fs::path good = root / L"good.xdelta";
    nla::write_text_file(good, add_only_delta("SCON4"));
    auto fit = nla::check_patch(good, disc);
    check(fit.opened, "a patch is opened");
    check(fit.windows == 1, "and its windows counted");
    check(fit.sampled == 1 && fit.matched == 1, "the one window decodes and matches");
    check(fit.error.empty(), "nothing is reported against it");

    check(nla::write_patch_manifest(disc, good, fit), "the launcher writes patch.manifest");
    check_text(nla::read_patch_delta(disc).wstring(), good.wstring(),
               "the manifest names the delta it was given");
    check(!nla::write_patch_manifest(disc, good, fit), "an unchanged manifest is not written");

    // What the manifest already records is what a second Apply uses: the delta is not
    // decoded again, and another delta is not mistaken for the one that was checked.
    auto recorded = nla::read_patch_fit(disc, good);
    check(recorded.has_value() && recorded->opened, "the manifest's fit is read back");
    check(!nla::read_patch_fit(disc, root / L"other.xdelta").has_value(),
          "a delta the manifest does not name has no recorded fit");

    // Something that is not a delta at all is refused rather than crashing.
    const fs::path junk = root / L"junk.xdelta";
    nla::write_text_file(junk, "this is not a patch");
    auto refused = nla::check_patch(junk, disc);
    check(!refused.opened && !refused.error.empty(), "a file that is not a patch is refused");
    check(nla::check_patch(root / L"absent.xdelta", disc).error.size() > 0,
          "a missing patch is reported");

    // A disc that has not been extracted has nothing to patch.
    std::error_code ec;
    fs::create_directories(root / L"empty", ec);
    auto unextracted = nla::check_patch(good, root / L"empty");
    check(!unextracted.opened && !unextracted.error.empty(),
          "an unextracted disc is reported");

    check(nla::write_patch_manifest(disc, {}, {}), "switching the patch off removes the manifest");
    check(!fs::exists(disc / L"patch.manifest"), "and it is gone");

    // The delta is not deleted when the patch is switched off, so switching it back on
    // re-checks the same file and writes the manifest again: no download, no second copy of
    // the disc, and the game is back on the patch.
    auto restored = nla::check_patch(good, disc);
    check(restored.opened && restored.sampled > 0 && restored.matched == restored.sampled,
          "the delta still fits the disc it was switched off from");
    check(nla::write_patch_manifest(disc, good, restored),
          "switching it back on writes the manifest again");
    check_text(nla::read_patch_delta(disc).wstring(), good.wstring(),
               "naming the same delta");
}

void catalog() {
    const std::wstring json =
        L"{\n"
        L"  \"version\": 1,\n"
        L"  \"mods\": [\n"
        L"    {\"id\": \"scon4\", \"name\": \"SCON4\", \"version\": \"2.1.0\",\n"
        L"     \"author\": \"SCON4 team\", \"description\": \"A \\\"disc patch\\\".\",\n"
        L"     \"disc_id\": [\"G4NJDA\"], \"kind\": \"patch\",\n"
        L"     \"release\": {\"owner\": \"NicholasMoser\", \"repo\": \"SCON4-Releases\",\n"
        L"                   \"asset\": \"patches.zip\", \"file\": \"vanilla.xdelta\"}},\n"
        L"    {\"id\": \"one-disc\", \"disc_id\": \"GNREDA\"},\n"
        L"    {\"id\": \"no-disc\", \"name\": \"Ignored\"}\n"
        L"  ]\n"
        L"}\n";
    std::wstring error;
    auto mods = nla::parse_mod_catalog(json, &error);
    check(error.empty(), "the catalog parses");
    check(mods.size() == 2, "an entry that names no disc is not offered");
    if (mods.size() == 2) {
        check_text(mods[0].id, L"scon4", "the entry keeps its id");
        check_text(mods[0].description, L"A \"disc patch\".", "escapes are read");
        check(mods[0].patch, "a patch entry is a patch");
        check(mods[0].downloadable(), "and it says where its files come from");
        check_text(mods[0].file, L"vanilla.xdelta", "the delta inside the asset is named");
        check(nla::catalog_supports(mods[0], L"g4njda"), "the disc it names is supported");
        check(!nla::catalog_supports(mods[0], L"GNREDA"), "another disc is not");
        check_text(mods[1].disc_ids[0], L"GNREDA", "one disc may be written as a string");
    }

    auto broken = nla::parse_mod_catalog(L"{\"mods\": [", &error);
    check(broken.empty() && !error.empty(), "a broken catalog is reported, not half-read");

    // The catalog the launcher ships is found from the build folder and reads.
    const fs::path shipped = nla::mod_catalog_path();
    check(!shipped.empty(), "the shipped mods.json is found");
    auto published = nla::load_mod_catalog(shipped, &error);
    if (error.empty() && published.empty()) error = L"the file holds no entries";
    check(error.empty() && !published.empty(),
          ("the shipped catalog reads: " + nla::narrow(error)).c_str());
}

void table(const fs::path& root) {
    std::vector<nla::CatalogMod> catalog(3);
    catalog[0].id = L"retuned-opening";
    catalog[0].name = L"Retuned Opening";
    catalog[0].version = L"1.0";
    catalog[0].disc_ids = {L"G4NJDA"};
    catalog[1].id = L"scon4";
    catalog[1].name = L"SCON4";
    catalog[1].version = L"2.1.0";
    catalog[1].patch = true;
    catalog[1].disc_ids = {L"G4NJDA"};
    catalog[1].owner = L"someone";
    catalog[1].repo = L"releases";
    catalog[2].id = L"another-build";
    catalog[2].disc_ids = {L"GNREDA"};

    write(root / L"mods" / L"retuned-opening" / L"mod.txt",
          L"name = Retuned (local)\ndisc_id = G4NJDA\n");
    write(root / L"mods" / L"hand-made" / L"mod.txt", L"name = Hand Made\ndisc_id = G4NJDA\n");
    write(root / L"mods" / L"elsewhere" / L"mod.txt", L"name = Elsewhere\ndisc_id = GNREDA\n");
    write(root / L"patches" / L"SCON4-2.1.0.xdelta", L"a delta");
    write(root / L"patches" / L"other-patch.xdelta", L"a delta");

    auto entries = nla::collect_mods(catalog, root / L"mods", root / L"patches", L"G4NJDA");
    check(entries.size() == 4, "the table is the catalog's rows for this disc plus the local ones");

    auto find = [&entries](const std::wstring& id) -> const nla::ModChoice* {
        for (const auto& entry : entries) {
            if (entry.id == id) return &entry;
        }
        return nullptr;
    };

    const nla::ModChoice* retuned = find(L"retuned-opening");
    check(retuned && retuned->known && retuned->installed,
          "a catalog entry whose folder is here is both known and installed");
    check(retuned && retuned->name == L"Retuned (local)",
          "the folder's own manifest is the truth about the mod");

    const nla::ModChoice* scon4 = find(L"scon4");
    check(scon4 && scon4->patch && scon4->installed, "a downloaded patch is attached to its row");
    check(scon4 && scon4->delta.filename().wstring() == L"SCON4-2.1.0.xdelta",
          "and the delta on disk names the row");
    check(find(L"patch:SCON4-2.1.0.xdelta") == nullptr,
          "a delta the catalog names is one row, not also a local one");

    const nla::ModChoice* handmade = find(L"hand-made");
    check(handmade && handmade->installed && !handmade->known,
          "a folder the catalog does not know is still a row");

    check(find(L"patch:other-patch.xdelta") != nullptr,
          "a delta with no catalog entry gets a row of its own");
    check(find(L"another-build") == nullptr && find(L"elsewhere") == nullptr,
          "rows for another disc are not in the table");

    // The same files read as another game's table: SCON4's delta is for G4NJDA only, so
    // sitting in the shared patches/ folder does not make it a row for another disc.
    auto other = nla::collect_mods(catalog, root / L"mods", root / L"patches", L"GNREDA");
    auto has = [&other](const std::wstring& id) {
        for (const auto& entry : other) {
            if (entry.id == id) return true;
        }
        return false;
    };
    check(other.size() == 3, "another disc's table is its own rows and the local deltas");
    check(!has(L"scon4") && !has(L"patch:SCON4-2.1.0.xdelta"),
          "a patch the catalog names is not offered for a disc it does not name");
    check(has(L"another-build") && has(L"elsewhere"),
          "the rows that name this disc are still there");
    check(!has(L"hand-made") && !has(L"retuned-opening"),
          "and the rows for the first disc are not");

    check(nla::unique_mod_id(entries, L"scon4") == L"scon4-2",
          "a wanted id that is taken gets a suffix");
    check(nla::unique_mod_id(entries, L"free") == L"free", "a free id is used as it is");
}

int run() {
    manifests();
    catalog();

    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec) / L"NarutoLauncher-mods-test";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) {
        std::printf("FAIL cannot create %s\n", nla::narrow(root.wstring()).c_str());
        return 1;
    }
    discovery(root / L"discovery");
    table(root / L"table");
    overlays(root / L"overlays");
    programs(root / L"programs");
    patches(root / L"patches");
    fs::remove_all(root, ec);

    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

// MinGW links with -municode on Windows, which wants wmain.
#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
