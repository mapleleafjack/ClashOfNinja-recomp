#include "tools_package.h"

#include <cstdint>
#include <fstream>

#include "tools.h"
#include "util.h"

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
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

// SHA-256 through CNG, so the check does not depend on a tool being installed or on the
// wording of an English-only command.
bool sha256_file(const fs::path& path, std::string& hex, wstring* error) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        *error = L"cannot open " + path.wstring();
        return false;
    }
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) {
        std::vector<char> buffer(1 << 16);
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            auto read = static_cast<ULONG>(file.gcount());
            if (read == 0) break;
            if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), read, 0) < 0) {
                ok = false;
                break;
            }
        }
    }
    unsigned char digest[32] = {};
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) {
        *error = L"cannot hash " + path.wstring();
        return false;
    }
    static const wchar_t* digits = L"0123456789abcdef";
    hex.clear();
    for (unsigned char byte : digest) {
        hex.push_back(static_cast<char>(digits[byte >> 4]));
        hex.push_back(static_cast<char>(digits[byte & 0xF]));
    }
    return true;
}

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

#endif  // _WIN32

}  // namespace

bool tools_package_available() {
#if defined(_WIN32)
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
#if defined(_WIN32)
    fs::path archive = fs::path(temp_dir()) / L"NarutoLauncher-tools.zip";
    std::wstring url = widen(kToolsUrl);
    std::string expected = kToolsSha256;

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
                         std::string hex;
                         wstring error;
                         if (!sha256_file(archive, hex, &error)) {
                             job.log(error + L"\n");
                             return 1;
                         }
                         if (hex != expected) {
                             job.log(format(L"[launcher] the archive hashes to %ls, not %ls\n",
                                            widen(hex).c_str(), widen(expected).c_str()));
                             std::error_code ec;
                             fs::remove(archive, ec);
                             return 1;
                         }
                         job.log(L"[launcher] the checksum matches\n");
                         return 0;
                     }});
    tasks.push_back({L"Unpacking the build tools…", [archive](Job& job) -> int {
                         auto tar = find_tool(L"tar" + wstring(exe_suffix()));
                         if (tar.empty()) {
                             job.log(L"[launcher] tar.exe was not found; Windows 10 1803 "
                                     L"and later ship it\n");
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
