#pragma once

#include <optional>
#include <string>

#include "project.h"
#include "settings.h"

namespace nla {

// The game the launcher is working on, the settings it remembers between runs, and what the
// project's own files say about it.
//
// The three have to agree. A status read before the mods' instruction words were written
// would offer a build that is not the game on disk, and a project replaced without its status
// re-read would offer one for a disc that is no longer there. Both of those were the caller's
// to remember; here the status is set with the project, so there is nothing to forget.
class ProjectState {
  public:
    // The settings from disk, and the project in the folder the launcher works in.
    void open();
    // Settings only, for a caller that puts its own project in place afterwards.
    void load_settings();

    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }
    void save() { settings_.save(); }

    // The project in use, or nothing when there is not one yet.
    const std::optional<Project>& project() const { return project_; }

    // Takes the project in use, and reads what its files say.
    void set_project(std::optional<Project> project);
    void set_project(const fs::path& root, std::wstring game);
    // The same folder, a different game in it.
    void select_game(const std::wstring& folder);
    // There is no project to work on: the folders have not been set up yet.
    void clear_project() { set_project(std::nullopt); }

    // The disc or the build changed under the project, so what its files say changed with
    // it. Reads the disc's digest, which is not something to do per frame.
    void reload_status();
    const ProjectStatus& status() const { return status_; }
    // A page standing in for one that is not on disk: a render, or a state the launcher is
    // about to be in.
    void set_status(ProjectStatus status) { status_ = std::move(status); }

  private:
    Settings settings_;
    std::optional<Project> project_;
    ProjectStatus status_;
};

// The folder the launcher works in, if it is there. Locating looks at the machine rather than
// at what the launcher has loaded, so it stays out of the state it produces.
std::optional<fs::path> locate_project_folder();

}  // namespace nla
