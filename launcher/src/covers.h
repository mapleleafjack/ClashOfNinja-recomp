#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace nla {

namespace fs = std::filesystem;

// One game's box art, read once and kept for later frames.
struct Cover {
    bool loaded = false;
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

// The box art the launcher draws, by game.
//
// A folder is read once and the answer kept, including "there is none", so a page that
// redraws every frame does not go back to the disk for it. The keys carry the project root,
// so a moved or newly chosen project folder does not pick up art from the old one.
class CoverCache {
  public:
    // The art stored under `key`, or nullptr when nothing under it was an image. `paths` is
    // tried in order on the first ask and only then.
    const Cover* load(const std::wstring& key, const std::vector<fs::path>& paths);

    // What is drawn in a cover's place until covers/ gets a file for that game: a still, dark
    // box with the launcher's accent glowing behind where the logo sits, so an empty cover
    // looks deliberate rather than broken.
    const Cover& placeholder();

    // Forgotten after a fetch, so the files that arrived are read on the next frame.
    void clear() { covers_.clear(); }
    std::size_t size() const { return covers_.size(); }

  private:
    std::map<std::wstring, Cover> covers_;
    Cover placeholder_;
};

}  // namespace nla
