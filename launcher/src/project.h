#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nla {

namespace fs = std::filesystem;

// One of the games the project builds: its own folder under the project root, holding a
// CMakeLists.txt that names the game and the disc it is checked against.
struct GameInfo {
    std::wstring folder;       // "naruto", "naruto2", "gnt3", "gnt4"
    std::wstring title;        // the game's name, as the build calls it
    std::wstring short_title;  // what the launcher's game switch has room for
};

struct DiscStatus {
    bool extracted = false;
    bool verified = false;
    std::wstring sha1;
    std::wstring expected;
};

struct ProjectStatus {
    DiscStatus disc;
    bool built = false;
};

class Project {
  public:
    explicit Project(fs::path root, std::wstring game = {});

    static const std::vector<GameInfo>& games();
    static const GameInfo& game_info(const std::wstring& folder);
    static std::wstring default_game();

    bool valid() const;

    fs::path root() const { return root_; }
    std::wstring game() const { return game_; }
    std::wstring title() const;
    fs::path game_dir() const;
    // The box art the launcher shows, one file per game: covers/naruto.png. It sits
    // beside the game folders rather than in one, so art survives a game folder being
    // made or replaced.
    fs::path covers_dir() const;
    fs::path disc() const;
    fs::path dol() const;
    fs::path manifest() const;
    fs::path build_dir() const;
    fs::path binary() const;
    fs::path controls() const;
    fs::path saves() const;
    fs::path extract_script() const;
    fs::path cmake_lists() const;

    std::wstring disc_id() const;
    std::wstring expected_sha1() const;
    DiscStatus disc_status() const;
    ProjectStatus status() const;

    static std::optional<fs::path> locate(const std::wstring& saved);

  private:
    fs::path root_;
    std::wstring game_;
};

}  // namespace nla
