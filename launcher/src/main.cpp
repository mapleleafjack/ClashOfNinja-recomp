#include "app.h"
#include "tools.h"
#include "tools_package.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// A render stands in for a window of a given size when one is named, as "2560x1417".
void parse_window_size(const char* text, int* width, int* height) {
    int w = 0, h = 0;
    if (text && std::sscanf(text, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
        *width = w;
        *height = h;
    }
}

}  // namespace

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    nla::initialise_environment();
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && wcscmp(argv[1], L"--render") == 0) {
        nla::App app;
        std::string view = argc >= 4 ? nla::narrow(argv[3]) : std::string("main");
        int width = 0, height = 0;
        if (argc >= 5) parse_window_size(nla::narrow(argv[4]).c_str(), &width, &height);
        int code = app.render_to(nla::narrow(argv[2]), 1.0f, view, width, height);
        LocalFree(argv);
        return code;
    }
    // The release check fetches the pinned tool archive without a window and reads the log.
    if (argv && argc >= 3 && wcscmp(argv[1], L"--fetch-tools") == 0) {
        int code = nla::fetch_tools_to_log(std::filesystem::path(argv[2]));
        LocalFree(argv);
        return code;
    }
    if (argv) LocalFree(argv);
    nla::App app;
    return app.create() ? app.run() : 1;
}
#else
int main(int argc, char** argv) {
    nla::initialise_environment();
    if (argc >= 3 && std::strcmp(argv[1], "--render") == 0) {
        nla::App app;
        std::string view = argc >= 4 ? argv[3] : "main";
        int width = 0, height = 0;
        if (argc >= 5) parse_window_size(argv[4], &width, &height);
        return app.render_to(argv[2], 1.0f, view, width, height);
    }
    if (argc >= 3 && std::strcmp(argv[1], "--fetch-tools") == 0) {
        return nla::fetch_tools_to_log(std::filesystem::path(argv[2]));
    }
    nla::App app;
    return app.create() ? app.run() : 1;
}
#endif
