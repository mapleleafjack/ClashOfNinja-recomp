#include "image.h"

#include <fstream>
#include <iterator>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

namespace nla {

bool decode_image(const unsigned char* data, size_t size, int* width, int* height,
                  std::vector<std::uint32_t>& pixels) {
    int w = 0, h = 0, channels = 0;
    unsigned char* rgba = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels,
                                                4);
    if (!rgba || w <= 0 || h <= 0) {
        if (rgba) stbi_image_free(rgba);
        return false;
    }
    pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < pixels.size(); ++i) {
        auto alpha = static_cast<std::uint32_t>(rgba[i * 4 + 3]);
        pixels[i] = (alpha << 24) | (static_cast<std::uint32_t>(rgba[i * 4]) << 16) |
                    (static_cast<std::uint32_t>(rgba[i * 4 + 1]) << 8) |
                    static_cast<std::uint32_t>(rgba[i * 4 + 2]);
    }
    stbi_image_free(rgba);
    if (width) *width = w;
    if (height) *height = h;
    return true;
}

bool load_image(const std::filesystem::path& path, int* width, int* height,
                std::vector<std::uint32_t>& pixels) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;
    return decode_image(bytes.data(), bytes.size(), width, height, pixels);
}

}  // namespace nla
