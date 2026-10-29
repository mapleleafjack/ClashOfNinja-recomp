#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace nla {

// Decodes a PNG or JPEG into straight-alpha 0xAARRGGBB pixels.
bool decode_image(const unsigned char* data, size_t size, int* width, int* height,
                  std::vector<std::uint32_t>& pixels);

// The same, from a file on disk.
bool load_image(const std::filesystem::path& path, int* width, int* height,
                std::vector<std::uint32_t>& pixels);

}  // namespace nla
