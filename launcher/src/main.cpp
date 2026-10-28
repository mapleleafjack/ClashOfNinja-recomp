#include "app.h"
#include "tools.h"
#include "tools_package.h"
#include "util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
        int code = app.render_to(nla::narrow(argv[2]), 1.0f, view);
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
        return app.render_to(argv[2], 1.0f, view);
    }
    if (argc >= 3 && std::strcmp(argv[1], "--fetch-tools") == 0) {
        return nla::fetch_tools_to_log(std::filesystem::path(argv[2]));
    }
    nla::App app;
    return app.create() ? app.run() : 1;
}
#endif
