#include "shortcut.h"

#include "util.h"

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#elif !defined(__APPLE__)
// Linux writes a desktop entry; nothing extra is needed.
#endif

namespace nla {
namespace {

#if defined(_WIN32)
fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path)) || !path) return {};
    fs::path result(path);
    CoTaskMemFree(path);
    return result;
}
#else
#if !defined(__APPLE__)
bool write_text(const fs::path& path, const wstring& text, wstring* error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (!write_text_file(path, narrow(text))) {
        if (error) *error = L"cannot write " + path.wstring();
        return false;
    }
    return true;
}
#endif
#endif

#if defined(__APPLE__)
// On macOS the launcher lives inside a .app, and the shortcut should point at the bundle
// rather than at the executable buried in Contents/MacOS.
fs::path shortcut_target(const fs::path& exe) {
    auto text = exe.wstring();
    auto marker = text.find(L".app/");
    if (marker != wstring::npos) return fs::path(text.substr(0, marker + 4));
    return exe;
}
#endif

}  // namespace

fs::path desktop_folder() {
#if defined(_WIN32)
    return known_folder(FOLDERID_Desktop);
#else
    return home_dir() / L"Desktop";
#endif
}

fs::path applications_folder() {
#if defined(_WIN32)
    return known_folder(FOLDERID_Programs);
#elif defined(__APPLE__)
    return home_dir() / L"Applications";
#else
    auto data = environment(L"XDG_DATA_HOME");
    fs::path base = data.empty() ? (home_dir() / L".local" / L"share") : fs::path(data);
    return base / L"applications";
#endif
}

const wchar_t* shortcut_folder_label() {
#if defined(_WIN32)
    return L"Start menu shortcut";
#elif defined(__APPLE__)
    return L"Applications shortcut";
#else
    return L"Menu shortcut";
#endif
}

std::wstring shortcut_name() {
#if defined(_WIN32)
    return L"Naruto - Clash of Ninja.lnk";
#elif defined(__APPLE__)
    return L"Naruto - Clash of Ninja";
#else
    return L"naruto-clash-of-ninja.desktop";
#endif
}

bool create_shortcut(const fs::path& link, const fs::path& target, const fs::path& working_dir,
                     const std::wstring& description, std::wstring* error) {
#if defined(_WIN32)
    IShellLinkW* shell_link = nullptr;
    HRESULT result =
        CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                         reinterpret_cast<void**>(&shell_link));
    if (FAILED(result) || !shell_link) {
        if (error) *error = L"cannot create a shell link";
        return false;
    }
    shell_link->SetPath(target.c_str());
    shell_link->SetWorkingDirectory(working_dir.c_str());
    shell_link->SetDescription(description.c_str());
    shell_link->SetIconLocation(target.c_str(), 0);

    IPersistFile* file = nullptr;
    result = shell_link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (FAILED(result) || !file) {
        shell_link->Release();
        if (error) *error = L"cannot write a shell link";
        return false;
    }
    std::error_code ec;
    fs::create_directories(link.parent_path(), ec);
    result = file->Save(link.c_str(), TRUE);
    file->Release();
    shell_link->Release();
    if (FAILED(result)) {
        if (error) *error = L"cannot save " + link.wstring();
        return false;
    }
    return true;
#elif defined(__APPLE__)
    std::error_code ec;
    fs::remove(link, ec);
    fs::create_directories(link.parent_path(), ec);
    fs::create_symlink(shortcut_target(target), link, ec);
    if (ec) {
        if (error) *error = L"cannot create " + link.wstring() + L": " + widen(ec.message());
        return false;
    }
    return true;
#else
    const fs::path entry = link;
    wstring text;
    text += L"[Desktop Entry]\n";
    text += L"Type=Application\n";
    text += L"Name=Naruto: Clash of Ninja\n";
    text += L"Comment=" + description + L"\n";
    text += L"Exec=" + quote_arg(target.wstring()) + L"\n";
    text += L"Path=" + working_dir.wstring() + L"\n";
    text += L"Terminal=false\n";
    text += L"Categories=Game;\n";
    if (!write_text(entry, text, error)) return false;
    std::error_code ec;
    fs::permissions(entry, fs::perms::owner_all, ec);
    return true;
#endif
}

}  // namespace nla
