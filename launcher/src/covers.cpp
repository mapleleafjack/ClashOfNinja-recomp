#include "covers.h"

#include <algorithm>
#include <cmath>
#include <system_error>

#include "image.h"
#include "painter.h"

namespace nla {

const Cover* CoverCache::load(const std::wstring& key, const std::vector<fs::path>& paths) {
    auto found = covers_.find(key);
    if (found != covers_.end()) return found->second.loaded ? &found->second : nullptr;

    Cover art;
    std::error_code ec;
    for (const fs::path& path : paths) {
        if (!fs::is_regular_file(path, ec)) continue;
        if (load_image(path, &art.width, &art.height, art.pixels)) {
            art.loaded = true;
            break;
        }
    }
    Cover& stored = covers_.emplace(key, std::move(art)).first->second;
    return stored.loaded ? &stored : nullptr;
}

const Cover& CoverCache::placeholder() {
    if (placeholder_.loaded) return placeholder_;

    constexpr int kWidth = 300;
    constexpr int kHeight = 400;
    const float glow_x = kWidth * 0.5f;
    const float glow_y = kHeight * 0.33f;
    const float glow_radius = kWidth * 0.62f;

    placeholder_.width = kWidth;
    placeholder_.height = kHeight;
    placeholder_.pixels.resize(static_cast<size_t>(kWidth) * kHeight);
    const Color top = rgb(0x262d39);
    const Color bottom = rgb(0x151920);
    const Color accent = rgb(theme::Accent);
    for (int y = 0; y < kHeight; ++y) {
        float t = static_cast<float>(y) / (kHeight - 1);
        for (int x = 0; x < kWidth; ++x) {
            float distance =
                std::hypot(static_cast<float>(x) - glow_x, static_cast<float>(y) - glow_y) /
                glow_radius;
            float glow = std::pow(std::clamp(1.0f - distance, 0.0f, 1.0f), 2.4f) * 0.30f;
            auto channel = [&](float from, float to, float light) {
                float value = from + (to - from) * t;
                value += (light - value) * glow;
                return static_cast<std::uint32_t>(
                    std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            };
            std::uint32_t red = channel(top.r, bottom.r, accent.r);
            std::uint32_t green = channel(top.g, bottom.g, accent.g);
            std::uint32_t blue = channel(top.b, bottom.b, accent.b);
            placeholder_.pixels[static_cast<size_t>(y) * kWidth + x] =
                0xFF000000u | (red << 16) | (green << 8) | blue;
        }
    }
    placeholder_.loaded = true;
    return placeholder_;
}

}  // namespace nla
