#include "dialog.h"

#if defined(_WIN32)

#include <SDL_syswm.h>
#include <objbase.h>
#include <shobjidl.h>
#include <windows.h>

namespace nla {
namespace {

// The dialog is shown on the thread that owns the window, where SDL has already
// initialised COM; this balances that count, and copes with a mode that differs.
class ComScope {
  public:
    ComScope() {
        HRESULT result =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        owned_ = SUCCEEDED(result);
    }
    ~ComScope() {
        if (owned_) CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

  private:
    bool owned_ = false;
};

template <typename T>
void release(T* pointer) {
    if (pointer) pointer->Release();
}

HWND owner_window(SDL_Window* parent) {
    if (!parent) return nullptr;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(parent, &info)) return nullptr;
    if (info.subsystem != SDL_SYSWM_WINDOWS) return nullptr;
    return info.info.win.window;
}

// The folder to open the dialog in, and the file to preselect there.
void split_start(const fs::path& start, fs::path* folder, std::wstring* name) {
    if (start.empty()) return;
    std::error_code ec;
    if (fs::is_directory(start, ec)) {
        *folder = start;
        return;
    }
    *folder = start.parent_path();
    *name = start.filename().wstring();
}

}  // namespace

DialogAnswer show_native_dialog(DialogKind kind, SDL_Window* parent,
                                const std::wstring& title, const fs::path& start) {
    ComScope com;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog))))
        return {};

    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        options |= kind == DialogKind::Folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
        dialog->SetOptions(options);
    }
    if (!title.empty()) dialog->SetTitle(title.c_str());
    if (kind == DialogKind::Image) {
        static const COMDLG_FILTERSPEC images[] = {
            {L"Disc images", L"*.iso;*.ciso;*.rvz;*.wia;*.gcz;*.wbfs;*.tgc;*.nfs"},
            {L"All files", L"*.*"},
        };
        dialog->SetFileTypes(2, images);
        dialog->SetFileTypeIndex(1);
    }

    fs::path folder;
    std::wstring name;
    split_start(start, &folder, &name);
    if (!folder.empty()) {
        IShellItem* item = nullptr;
        auto text = folder.wstring();
        // SetFolder makes the dialog open there; SetDefaultFolder would only apply the
        // first time this dialog is shown.
        if (SUCCEEDED(SHCreateItemFromParsingName(text.c_str(), nullptr, IID_IShellItem,
                                                  reinterpret_cast<void**>(&item)))) {
            dialog->SetFolder(item);
            release(item);
        }
    }
    if (!name.empty()) dialog->SetFileName(name.c_str());

    DialogAnswer answer;
    answer.shown = true;
    HRESULT result = dialog->Show(owner_window(parent));
    if (SUCCEEDED(result)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR text = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &text))) {
                answer.path = fs::path(text);
                CoTaskMemFree(text);
            }
            release(item);
        }
    } else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        // Closing the dialog is an answer of "nothing", not a reason to fall back.
    } else {
        answer.shown = false;
    }
    release(dialog);
    return answer;
}

}  // namespace nla

#elif !defined(__APPLE__)

#include "tools.h"

#include <cerrno>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace nla {
namespace {

struct Capture {
    // Whether the program ran at all, which is what tells "cancelled" from "not there".
    bool ran = false;
    bool ok = false;
    std::string output;
};

// Zenity and kdialog both print the chosen path on stdout and exit non-zero when the
// user cancels.
Capture run_capture(const std::vector<std::string>& argv) {
    Capture capture;
    if (argv.empty()) return capture;
    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const auto& arg : argv) raw.push_back(const_cast<char*>(arg.c_str()));
    raw.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) return capture;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return capture;
    }
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        execvp(raw[0], raw.data());
        _exit(127);
    }
    close(fds[1]);

    char buffer[512];
    for (;;) {
        ssize_t got = read(fds[0], buffer, sizeof buffer);
        if (got > 0) {
            capture.output.append(buffer, static_cast<size_t>(got));
            continue;
        }
        if (got < 0 && errno == EINTR) continue;
        break;
    }
    close(fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    capture.ran = WIFEXITED(status) && WEXITSTATUS(status) != 127;
    capture.ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    while (!capture.output.empty() &&
           (capture.output.back() == '\n' || capture.output.back() == '\r'))
        capture.output.pop_back();
    return capture;
}

std::string start_argument(const fs::path& start) {
    return start.empty() ? std::string(".") : start.string();
}

DialogAnswer from_capture(const Capture& capture) {
    DialogAnswer answer;
    answer.shown = capture.ran;
    if (capture.ok && !capture.output.empty()) answer.path = fs::path(widen(capture.output));
    return answer;
}

}  // namespace

DialogAnswer show_native_dialog(DialogKind kind, SDL_Window*, const std::wstring& title,
                                const fs::path& start) {
    auto zenity = find_tool(L"zenity");
    if (!zenity.empty()) {
        std::vector<std::string> argv{narrow(zenity), "--file-selection",
                                      "--title=" + narrow(title)};
        argv.push_back(kind == DialogKind::Folder ? "--directory" : "--file");
        auto folder = start_argument(start);
        if (kind == DialogKind::Folder && folder.back() != '/') folder += '/';
        argv.push_back("--filename=" + folder);
        return from_capture(run_capture(argv));
    }

    auto kdialog = find_tool(L"kdialog");
    if (kdialog.empty()) return {};
    std::vector<std::string> argv{narrow(kdialog)};
    if (kind == DialogKind::Folder) {
        argv.push_back("--getexistingdirectory");
        argv.push_back(start_argument(start));
    } else {
        argv.push_back("--getopenfilename");
        argv.push_back(start_argument(start));
        argv.push_back("*.iso *.ciso *.rvz *.wia *.gcz *.wbfs *.tgc *.nfs|Disc images");
    }
    return from_capture(run_capture(argv));
}

}  // namespace nla

#endif
