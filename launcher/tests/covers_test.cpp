// Checks the box art cache: the stand-in cover it draws, and that a game's folder is read
// once and its answer kept - including "there is none", which is the answer most machines
// give. Run by ctest; needs no game and no network.

#include "covers.h"

#include <cstdio>
#include <set>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

void stand_in() {
    nla::CoverCache cache;
    const nla::Cover& cover = cache.placeholder();
    check(cover.loaded, "there is a stand-in cover to draw");
    check(cover.width == 300 && cover.height == 400, "it is the size the tiles expect");
    check(cover.pixels.size() == static_cast<size_t>(cover.width) * cover.height,
          "it has a pixel for every place in that size");

    bool opaque = true;
    for (std::uint32_t pixel : cover.pixels) {
        if ((pixel >> 24) != 0xFFu) {
            opaque = false;
            break;
        }
    }
    check(opaque, "every pixel is opaque, so the tile behind cannot show through");

    // The cover is a gradient, and the glow is not centred: the two corners must differ,
    // which is what says the loop wrote more than one value.
    const std::uint32_t top_left = cover.pixels.front();
    const std::uint32_t bottom_left =
        cover.pixels[static_cast<size_t>(cover.height - 1) * cover.width];
    std::set<std::uint32_t> shades(cover.pixels.begin(), cover.pixels.end());
    check(top_left != bottom_left, "it shades from the top of the tile to the bottom");
    check(shades.size() > 100, "it is a gradient rather than a flat colour");

    const nla::Cover& again = cache.placeholder();
    check(&again == &cover, "the stand-in is drawn once and then kept");
}

void reading() {
    nla::CoverCache cache;
    check(cache.size() == 0, "a fresh cache holds nothing");

    const fs::path nowhere = fs::temp_directory_path() / L"NarutoLauncher-covers-test";
    std::error_code ec;
    fs::remove_all(nowhere, ec);

    check(cache.load(L"key", {nowhere / L"a.png", nowhere / L"b.png"}) == nullptr,
          "a game with no art has no cover");
    check(cache.size() == 1, "and that answer is kept, so the disk is asked once");

    // Asking again must not go back to the disk: the answer is already there.
    check(cache.load(L"key", {nowhere / L"c.png"}) == nullptr,
          "asking again keeps the first answer, not a second look");
    check(cache.size() == 1, "and does not store the key twice");

    check(cache.load(L"other", {}) == nullptr, "no candidate paths is no cover");

    cache.clear();
    check(cache.size() == 0, "clearing forgets what was read, after a fetch fills covers/");
}

int run() {
    stand_in();
    reading();
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
