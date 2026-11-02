#include "tools_package.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <thread>

#include "tools.h"
#include "util.h"

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

namespace nla {
namespace {

// build-release.ps1 -Split writes the archive and prints its SHA-256; a release passes
// these three to CMake so the launcher can only ever fetch the bytes it was built against.
#ifndef NLA_TOOLS_URL
#define NLA_TOOLS_URL ""
#endif
#ifndef NLA_TOOLS_SHA256
#define NLA_TOOLS_SHA256 ""
#endif
#ifndef NLA_TOOLS_BYTES
#define NLA_TOOLS_BYTES 0
#endif

const char* const kToolsUrl = NLA_TOOLS_URL;
const char* const kToolsSha256 = NLA_TOOLS_SHA256;
const uint64_t kToolsBytes = static_cast<uint64_t>(NLA_TOOLS_BYTES);

#if defined(_WIN32)

struct WinHttpHandle {
    HINTERNET handle = nullptr;
    ~WinHttpHandle() {
        if (handle) WinHttpCloseHandle(handle);
    }
};

bool download(Job& job, const std::wstring& url, const fs::path& out, wstring* error) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        *error = L"cannot read the download address: " + url;
        return false;
    }
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    WinHttpHandle session, connection, request;
    session.handle = WinHttpOpen(L"NarutoLauncher", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.handle) {
        *error = L"WinHTTP could not start: " + last_error();
        return false;
    }
    // A release download is served from github.com and redirected to another host.
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(session.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                     sizeof(redirect));

    connection.handle = WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0);
    if (!connection.handle) {
        *error = L"cannot reach " + host + L": " + last_error();
        return false;
    }
    request.handle = WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request.handle || !WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        *error = L"the download did not start: " + last_error();
        return false;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request.handle,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        *error = format(L"the server answered %lu for %ls", status, url.c_str());
        return false;
    }

    uint64_t total = 0;
    DWORD length = 0;
    DWORD length_size = sizeof(length);
    if (WinHttpQueryHeaders(request.handle,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &length_size,
                            WINHTTP_NO_HEADER_INDEX)) {
        total = length;
    }
    if (total == 0) total = kToolsBytes;

    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    if (!file) {
        *error = L"cannot write " + out.wstring();
        return false;
    }

    std::vector<unsigned char> buffer(1 << 16);
    uint64_t received = 0;
    int last_percent = -1;
    for (;;) {
        if (job.cancel_flag().load()) {
            *error = L"cancelled";
            return false;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available)) {
            *error = L"the download stopped early: " + last_error();
            return false;
        }
        if (available == 0) break;
        DWORD wanted = static_cast<DWORD>(
            available < buffer.size() ? available : buffer.size());
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, buffer.data(), wanted, &read)) {
            *error = L"the download stopped early: " + last_error();
            return false;
        }
        if (read == 0) break;
        file.write(reinterpret_cast<const char*>(buffer.data()), read);
        received += read;
        if (total > 0) {
            int percent = static_cast<int>(received * 100 / total);
            if (percent != last_percent) {
                job.progress(percent);
                last_percent = percent;
            }
        }
    }
    file.close();
    if (total > 0 && received != total) {
        *error = format(L"the download is %llu bytes, not %llu", received, total);
        return false;
    }
    return true;
}

#elif defined(__linux__)

// The archive a Linux release ships beside the launcher. A tar, not a zip: the launcher
// unpacks it with the tar every Linux has, rather than looking for an extractor.
constexpr wchar_t kArchiveName[] = L"NarutoLauncher-tools.tar.xz";

// curl fetches it, with the growing file watched from here so the panel has a real bar.
// curl's own progress goes to stderr as carriage returns, and the job's reader splits on
// newlines, so it would only appear once the download had already ended.
bool download(Job& job, const std::wstring& url, const fs::path& out, wstring* error) {
    auto curl = curl_tool();
    if (curl.empty()) {
        if (error) *error = L"curl was not found, and the launcher uses it to fetch the tools";
        return false;
    }
    std::error_code ec;
    fs::remove(out, ec);

    std::atomic_bool finished{false};
    std::thread watcher([&job, &out, &finished] {
        // A local, so the compiler does not fold a build with no pinned size into a
        // division by zero.
        const uint64_t total = kToolsBytes;
        while (!finished.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (total == 0) continue;
            uint64_t size = nla::file_size(out);
            if (size > 0) {
                uint64_t percent = size * 100 / total;
                job.progress(static_cast<int>(percent > 100 ? 100 : percent));
            }
        }
    });
    int code = job.run_process(
        curl,
        {L"--location", L"--fail", L"--silent", L"--show-error", L"--retry", L"2",
         L"--output", out.wstring(), url},
        temp_dir());
    finished.store(true);
    watcher.join();

    if (job.cancelled()) {
        *error = L"cancelled";
        return false;
    }
    if (code != 0) {
        *error = format(L"curl exited with status %d while fetching the tools", code);
        return false;
    }
    return true;
}

#endif

}  // namespace

// The check itself has nothing platform about it, so it sits outside the two blocks above:
// what differs between Windows and Linux is how the archive is fetched and unpacked, not
// what makes a good one. The digest is taken here rather than by the system's sha256sum or
// by a library, so the answer is the same on every machine.
bool check_archive(const fs::path& archive, const std::wstring& expected,
                   std::wstring* error) {
    const wstring hex = sha256_file(archive, error);
    if (hex.empty()) return false;
    if (hex == expected) return true;
    *error = format(L"the archive hashes to %ls, not %ls", hex.c_str(), expected.c_str());
    std::error_code ec;
    fs::remove(archive, ec);
    return false;
}

bool tools_package_available() {
#if defined(_WIN32) || defined(__linux__)
    return kToolsUrl[0] != '\0' && kToolsSha256[0] != '\0';
#else
    return false;
#endif
}

std::wstring tools_package_size() {
    if (kToolsBytes == 0) return {};
    return format(L"%llu MB", (kToolsBytes + (1 << 20) - 1) >> 20);
}

std::vector<Task> tools_package_tasks() {
#if defined(_WIN32) || defined(__linux__)
#if defined(_WIN32)
    fs::path archive = fs::path(temp_dir()) / L"NarutoLauncher-tools.zip";
#else
    fs::path archive = fs::path(temp_dir()) / kArchiveName;
#endif
    std::wstring url = widen(kToolsUrl);
    const std::wstring expected = widen(kToolsSha256);

    std::vector<Task> tasks;
    tasks.push_back({L"Downloading the build tools…", [archive, url](Job& job) -> int {
                         job.log(format(L"[launcher] %ls\n", url.c_str()));
                         wstring error;
                         if (!download(job, url, archive, &error)) {
                             job.log(error + L"\n");
                             return job.cancelled() ? 130 : 1;
                         }
                         job.progress(100);
                         return 0;
                     }});
    tasks.push_back({L"Checking the download…", [archive, expected](Job& job) -> int {
                         wstring error;
                         if (!check_archive(archive, expected, &error)) {
                             job.log(error + L"\n");
                             return 1;
                         }
                         job.log(L"[launcher] the checksum matches\n");
                         return 0;
                     }});
    tasks.push_back({L"Unpacking the build tools…", [archive](Job& job) -> int {
                         auto tar = find_tool(L"tar" + wstring(exe_suffix()));
                         if (tar.empty()) {
#if defined(_WIN32)
                             job.log(L"[launcher] tar.exe was not found; Windows 10 1803 "
                                     L"and later ship it\n");
#else
                             job.log(L"[launcher] tar was not found, and the launcher uses "
                                     L"it to unpack the tools\n");
#endif
                             return 127;
                         }
                         int code = job.run_process(
                             tar, {L"-xf", archive.wstring(), L"-C", exe_dir()}, home_dir());
                         std::error_code ec;
                         fs::remove(archive, ec);
                         return code;
                     }});
    tasks.push_back({L"Looking for the tools…", [](Job& job) -> int {
                         rescan_environment();
                         job.log(L"[launcher] the tools are in place\n");
                         return 0;
                     }});
    return tasks;
#else
    return {};
#endif
}

int fetch_tools_to_log(const fs::path& log) {
    std::vector<Task> tasks = tools_package_tasks();
    if (tasks.empty()) return 1;
    return run_tasks_to_log(std::move(tasks), log);
}

}  // namespace nla
