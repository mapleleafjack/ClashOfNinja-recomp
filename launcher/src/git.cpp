#include "git.h"

#include "tools.h"
#include "util.h"

namespace nla {
namespace {

std::wstring git(const fs::path& root, const std::vector<std::wstring>& args, int* status) {
    auto exe = git_tool();
    if (exe.empty()) {
        if (status) *status = -1;
        return {};
    }
    std::vector<std::wstring> all{L"-C", root.wstring()};
    all.insert(all.end(), args.begin(), args.end());
    std::string output;
    int code = -1;
    if (!capture_output(exe, all, &output, &code)) {
        if (status) *status = -1;
        return {};
    }
    if (status) *status = code;
    return trim(widen(output));
}

}  // namespace

std::wstring git_tool() {
#if defined(_WIN32)
    return find_tool_from({L"C:\\Program Files\\Git\\cmd\\git.exe"}, L"git.exe");
#else
    return find_tool_from({L"/usr/bin/git", L"/usr/local/bin/git", L"/opt/homebrew/bin/git"},
                          L"git");
#endif
}

bool project_is_checkout(const fs::path& root) {
    if (root.empty()) return false;
    int status = -1;
    git(root, {L"rev-parse", L"--git-dir"}, &status);
    return status == 0;
}

std::wstring project_revision(const fs::path& root) {
    if (root.empty()) return {};
    int status = -1;
    auto text = git(root, {L"describe", L"--tags", L"--always", L"--dirty"}, &status);
    return status == 0 ? text : std::wstring();
}

namespace {

long long project_source_time(const fs::path& root) {
    int status = -1;
    auto text = git(root, {L"log", L"-1", L"--format=%ct", L"HEAD"}, &status);
    if (status != 0 || text.empty()) return 0;
    try {
        return std::stoll(text);
    } catch (...) {
        return 0;
    }
}

}  // namespace

bool project_is_older_than(const fs::path& root, const std::wstring& commit, long long time) {
    if (root.empty()) return false;
    if (!commit.empty()) {
        int status = -1;
        git(root, {L"merge-base", L"--is-ancestor", commit, L"HEAD"}, &status);
        // 0: the checkout has that commit, so it is the same age or newer. 1: it certainly
        // does not. Anything else: the object was never fetched, so dates decide.
        if (status == 0) return false;
        if (status == 1) return true;
    }
    auto here = project_source_time(root);
    return time > 0 && here > 0 && here < time;
}

bool project_is_clean(const fs::path& root, std::wstring* reason) {
    int status = -1;
    auto text = git(root, {L"status", L"--porcelain"}, &status);
    if (status != 0) {
        if (reason) *reason = L"git could not read the project folder";
        return false;
    }
    if (text.empty()) return true;
    if (reason) *reason = L"the project folder has uncommitted changes; commit or stash "
                          L"them first";
    return false;
}

std::vector<Task> source_tasks(fs::path root) {
    return {
        {L"Checking the project folder…",
         [root](Job& job) {
             if (git_tool().empty()) {
                 job.log(L"[project] git was not found, so the source cannot be updated\n");
                 return 127;
             }
             if (!project_is_checkout(root)) {
                 job.log(L"[project] " + root.wstring() + L" is not a git checkout\n");
                 return 1;
             }
             std::wstring reason;
             if (!project_is_clean(root, &reason)) {
                 job.log(L"[project] " + reason + L"\n");
                 return 1;
             }
             job.log(L"[project] at " + project_revision(root) + L"\n");
             return 0;
         }},
        {L"Updating the game source…",
         [root](Job& job) {
             // --ff-only: a source update never merges or rebases local work.
             job.log(L"$ git -C " + root.wstring() + L" pull --ff-only --tags --prune\n");
             return job.run_process(git_tool(),
                                    {L"-C", root.wstring(), L"pull", L"--ff-only", L"--tags",
                                     L"--prune"},
                                    fs::path());
         }},
    };
}

}  // namespace nla
