#include "project_state.h"

namespace nla {

void ProjectState::load_settings() { settings_ = Settings::load(); }

void ProjectState::open() {
    load_settings();
    if (auto root = locate_project_folder()) set_project(*root, settings_.game);
}

void ProjectState::set_project(std::optional<Project> project) {
    project_ = std::move(project);
    reload_status();
}

void ProjectState::set_project(const fs::path& root, std::wstring game) {
    set_project(std::optional<Project>(Project(root, std::move(game))));
}

void ProjectState::select_game(const std::wstring& folder) {
    if (!project_) return;
    set_project(project_->root(), folder);
}

void ProjectState::reload_status() {
    status_ = project_ ? project_->status() : ProjectStatus{};
}

std::optional<fs::path> locate_project_folder() { return Project::locate(); }

}  // namespace nla
