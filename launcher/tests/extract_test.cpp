// Developer tool: extracts a disc image with the launcher's own extractor, so the
// port can be compared against tools/extract_disc.py. Not part of the launcher.
#include "disc.h"
#include "util.h"

#include <windows.h>

#include <atomic>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 3) {
        std::fwprintf(stderr, L"usage: extract_test <image> <outdir> [disc-id] [dol-sha1]\n");
        return 2;
    }
    nla::ExtractRequest request;
    request.image = argv[1];
    request.outdir = argv[2];
    request.disc_id = argc > 3 ? argv[3] : L"GNREDA";
    request.dol_sha1 = argc > 4 ? argv[4] : L"19251f7f15f8b6f7e0cd1c753409151ec49d9af2";

    std::atomic_bool cancel{false};
    auto outcome = nla::extract_disc(
        request,
        [](int percent, const std::wstring& stage) {
            std::wprintf(L"%3d%%  %ls\n", percent, stage.c_str());
        },
        cancel);
    if (!outcome.ok) {
        std::fwprintf(stderr, L"error: %ls\n", outcome.error.c_str());
        return 1;
    }
    std::wprintf(L"ok: %d files, %llu bytes, main.dol %ls\n", outcome.file_count,
                 static_cast<unsigned long long>(outcome.bytes), outcome.dol_sha1.c_str());
    return 0;
}
