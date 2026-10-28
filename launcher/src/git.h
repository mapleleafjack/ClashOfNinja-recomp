#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "jobs.h"

namespace nla {

namespace fs = std::filesystem;

// git, when it is installed; empty otherwise. The launcher works without it, and says so
// rather than failing when asked to update the source.
std::wstring git_tool();

// The project folder as a checkout git can read.
bool project_is_checkout(const fs::path& root);

// What the checkout is at, for people: "v1.1.0-3-gabc1234", or "-dirty" when modified.
// Empty when this is not a checkout.
std::wstring project_revision(const fs::path& root);

// True when the checkout predates the source the launcher was built from. `commit` and
// `time` describe that source: a checkout that holds the commit is not older, and one
// that has never fetched it is judged by its HEAD commit's date.
bool project_is_older_than(const fs::path& root, const std::wstring& commit, long long time);

// False, with the reason in `reason`, when the checkout has uncommitted changes; an
// update never overwrites those.
bool project_is_clean(const fs::path& root, std::wstring* reason);

// Fetches the newest source and fast-forwards onto it. Refuses a checkout with
// uncommitted changes, and anything that would need a merge or a rebase.
std::vector<Task> source_tasks(fs::path root);

}  // namespace nla
