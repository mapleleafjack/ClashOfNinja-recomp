// Checks what the launcher's own picker lists, and in what order: the parent folder first,
// then the folders, then the files the kind offers, each by name with the case ignored, and
// nothing whose name starts with a dot. None of that is visible from the drawing, so it is
// checked here against a folder made for the purpose. Run by ctest.

#include "picker.h"
#include "util.h"

#include <cstdio>
#include <string>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// A folder with the awkward cases in it: names that only sort correctly when the case is
// ignored, a dotfile, and files of kinds the picker should and should not offer.
void make_folder(const fs::path& root) {
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path inside = root / L"inside";
    fs::create_directories(inside / L"zebra", ec);
    fs::create_directories(inside / L"Apple", ec);
    fs::create_directories(inside / L"mango", ec);
    nla::write_text_file(inside / L".hidden", "# not offered\n");
    nla::write_text_file(inside / L"a.txt", "not a disc\n");
    nla::write_text_file(inside / L"Zelda.iso", "not read, only named\n");
    nla::write_text_file(inside / L"animal.iso", "not read, only named\n");
    nla::write_text_file(inside / L"hack.xdelta", "not read, only named\n");
}

std::wstring names(const std::vector<nla::PickerEntry>& entries) {
    std::wstring joined;
    for (const auto& entry : entries) {
        if (!joined.empty()) joined += L",";
        joined += entry.name;
    }
    return joined;
}

void listing() {
    const fs::path root = fs::temp_directory_path() / L"NarutoLauncher-picker-test";
    make_folder(root);

    {
        // An image picker inside a subfolder, so there is a parent to offer.
        const fs::path inside = root / L"inside";
        nla::Picker picker;
        picker.open(nla::PickerKind::Image, L"Choose", inside, {});
        const std::wstring got = names(picker.entries());
        check(got == L"..,Apple,mango,zebra,animal.iso,Zelda.iso",
              "an image picker lists the parent, the folders, then the disc images");
        check(picker.path() == inside.wstring(), "it opens on the folder it was given");
        check(picker.selected() == -1 && picker.offset() == 0 && picker.note().empty(),
              "a fresh list has nothing selected, nothing scrolled, and nothing to say");
    }
    {
        nla::Picker picker;
        picker.open(nla::PickerKind::Patch, L"Choose", root / L"inside", {});
        const std::wstring got = names(picker.entries());
        check(got == L"..,Apple,mango,zebra,hack.xdelta",
              "a patch picker offers the deltas and no disc images");
    }
    {
        nla::Picker picker;
        picker.open(nla::PickerKind::Folder, L"Choose", root / L"inside", {});
        const std::wstring got = names(picker.entries());
        check(got == L"..,Apple,mango,zebra", "a folder picker offers folders and no files");
    }
    {
        // Opened on a file: it starts in the folder that file is in, so the list has meaning.
        nla::Picker picker;
        picker.open(nla::PickerKind::Image, L"Choose", root / L"inside" / L"Zelda.iso", {});
        check(picker.folder() == (root / L"inside").wstring(),
              "a file to start from opens its own folder");
    }
    {
        // The parent of the root is a folder too, but going up from it is not offered.
        nla::Picker picker;
        picker.open(nla::PickerKind::Folder, L"Choose", root / L"inside" / L"mango", {});
        check(picker.entries().front().name == L"..",
              "a folder below the root still offers its parent");
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}

void answering() {
    nla::Picker picker;
    picker.open(nla::PickerKind::Image, L"Choose", L"/", {});
    check(picker.is_open(), "an open picker says so");
    picker.close();
    check(!picker.is_open(), "a closed picker says so");
    check(picker.entries().empty() && picker.note().empty(),
          "closing forgets the list and the note");
}

int run() {
    listing();
    answering();
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
