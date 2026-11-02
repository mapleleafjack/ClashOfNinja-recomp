#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "covers.h"
#include "project.h"
#include "project_state.h"
#include "screen.h"

namespace nla {

class PickerScreen;

// The game page: the project folder the launcher works in, the games in it, and the box art
// the covers are drawn with.
//
// It is the one page that changes the project, so it holds the state that records which game
// is in use rather than reading it through the window. A page that only reads it asks the
// host.
class GamesScreen : public Screen {
  public:
    GamesScreen(ScreenHost& host, ProjectState& model, PickerScreen& picker);

    // Stops the box-art worker between downloads, and joins it. Split in two because the
    // window lets it notice the stop while the other workers are being joined.
    void stop_fetching();
    void shutdown();

    // Asks where the game folders should go, and writes them there.
    void locate_project();
    void select_game(const std::wstring& folder);

    // All of them, always: the covers are the launcher's whole first page, and a game whose
    // folder does not exist yet gets it when a disc is chosen for it.
    std::vector<GameInfo> available_games() const;
    // A game's box art, or nullptr when it has none.
    const Cover* cover(const std::wstring& folder);
    // Fetches the art for the games that have none, so a fresh install shows covers without
    // the launcher shipping art the publisher owns.
    void fetch_covers();
    std::wstring current_title() const;
    // The tile drawn in a cover's place until covers/ gets a file for that game.
    const Cover& placeholder_tile();
    // The art is kept against the project folder, so a new one starts with none.
    void forget_art() { art_.clear(); }

    bool handles(Id id) const override;
    void activate(Id id) override;

  private:
    ScreenHost& host_;
    ProjectState& model_;
    PickerScreen& picker_;

    CoverCache art_;
    std::thread thread_;
    std::atomic_bool stop_{false};
    bool fetching_ = false;
};

}  // namespace nla
