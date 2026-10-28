#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nla {

// Decodes a PNG into straight-alpha 0xAARRGGBB pixels.
bool decode_png(const unsigned char* data, size_t size, int* width, int* height,
                std::vector<std::uint32_t>& pixels);

}  // namespace nla
