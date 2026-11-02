#pragma once

#include <cstddef>
#include <string>

#include "paths.h"

namespace nla {

wstring sha1_hex(const void* data, size_t size);
wstring sha1_file(const fs::path& path, wstring* error = nullptr);
// The same two for SHA-256, which is what a release publishes beside the tools archive. It
// is here rather than left to the system's sha256sum so that a download is checked the same
// way on every machine, whatever tools the player happens to have installed.
wstring sha256_hex(const void* data, size_t size);
wstring sha256_file(const fs::path& path, wstring* error = nullptr);

}  // namespace nla
