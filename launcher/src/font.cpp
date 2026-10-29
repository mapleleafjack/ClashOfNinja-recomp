#include "font.h"

#include "util.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

namespace nla {
namespace {

struct FaceSpec {
    float size;
    bool bold;
    bool mono;
};

FaceSpec spec(Face face) {
    switch (face) {
        case Face::Title: return {30.0f, true, false};
        case Face::Hero: return {40.0f, true, false};
        case Face::Subtitle: return {15.0f, false, false};
        case Face::CardTitle: return {18.0f, true, false};
        case Face::Body: return {16.0f, false, false};
        case Face::Small: return {14.0f, false, false};
        case Face::Tiny: return {12.5f, false, false};
        case Face::Mono: return {13.0f, false, true};
        case Face::Button: return {15.0f, true, false};
        case Face::ButtonLarge: return {20.0f, true, false};
    }
    return {15.0f, false, false};
}

enum class Kind { Sans, SansBold, Mono };

std::vector<fs::path> candidates(Kind kind) {
    std::vector<const wchar_t*> names;
#if defined(_WIN32)
    switch (kind) {
        case Kind::Sans:
            names = {L"segoeui.ttf", L"arial.ttf", L"tahoma.ttf", L"verdana.ttf"};
            break;
        case Kind::SansBold:
            names = {L"segoeuib.ttf", L"seguisb.ttf", L"arialbd.ttf", L"tahomabd.ttf"};
            break;
        case Kind::Mono:
            names = {L"consola.ttf", L"cour.ttf", L"lucon.ttf"};
            break;
    }
    fs::path root = environment(L"SystemRoot");
    if (root.empty()) root = fs::path(L"C:/Windows");
    std::vector<fs::path> paths;
    for (auto* name : names) paths.push_back(root / L"Fonts" / name);
    return paths;
#elif defined(__APPLE__)
    switch (kind) {
        case Kind::Sans:
            names = {L"/System/Library/Fonts/Helvetica.ttc",
                     L"/System/Library/Fonts/SFNS.ttf",
                     L"/System/Library/Fonts/Supplemental/Arial.ttf",
                     L"/Library/Fonts/Arial.ttf"};
            break;
        case Kind::SansBold:
            names = {L"/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                     L"/System/Library/Fonts/Supplemental/Helvetica Bold.ttf",
                     L"/System/Library/Fonts/Supplemental/Verdana Bold.ttf",
                     L"/Library/Fonts/Arial Bold.ttf"};
            break;
        case Kind::Mono:
            names = {L"/System/Library/Fonts/Menlo.ttc",
                     L"/System/Library/Fonts/Supplemental/Courier New.ttf",
                     L"/System/Library/Fonts/Supplemental/Andale Mono.ttf"};
            break;
    }
    std::vector<fs::path> paths;
    for (auto* name : names) paths.emplace_back(name);
    return paths;
#else
    switch (kind) {
        case Kind::Sans:
            names = {L"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                     L"/usr/share/fonts/TTF/DejaVuSans.ttf",
                     L"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                     L"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                     L"/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
                     L"/usr/share/fonts/truetype/freefont/FreeSans.ttf"};
            break;
        case Kind::SansBold:
            names = {L"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                     L"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                     L"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
                     L"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
                     L"/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
                     L"/usr/share/fonts/truetype/freefont/FreeSansBold.ttf"};
            break;
        case Kind::Mono:
            names = {L"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                     L"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                     L"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
                     L"/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
                     L"/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf"};
            break;
    }
    std::vector<fs::path> paths;
    for (auto* name : names) paths.emplace_back(name);
    return paths;
#endif
}

}  // namespace

struct TextEngine::Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    float size = 13.0f;
};

struct TextEngine::Glyph {
    int width = 0;
    int height = 0;
    int offset_x = 0;
    int offset_y = 0;
    std::vector<unsigned char> coverage;
};

struct TextEngine::Cache {
    std::unordered_map<std::uint64_t, Glyph> glyphs;
};

TextEngine::TextEngine() = default;
TextEngine::~TextEngine() = default;

bool TextEngine::initialise(wstring* error) {
    cache_ = std::make_unique<Cache>();

    auto load = [](Kind kind) -> std::unique_ptr<Font> {
        for (const auto& path : candidates(kind)) {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec)) continue;
            std::ifstream file(path, std::ios::binary);
            if (!file) continue;
            auto font = std::make_unique<Font>();
            file.seekg(0, std::ios::end);
            auto size = file.tellg();
            if (size <= 0) continue;
            file.seekg(0, std::ios::beg);
            font->data.resize(static_cast<size_t>(size));
            file.read(reinterpret_cast<char*>(font->data.data()),
                      static_cast<std::streamsize>(size));
            if (!file) continue;
            int offset = stbtt_GetFontOffsetForIndex(font->data.data(), 0);
            if (offset < 0) continue;
            if (!stbtt_InitFont(&font->info, font->data.data(), offset)) continue;
            return font;
        }
        return nullptr;
    };

    regular_ = load(Kind::Sans);
    mono_ = load(Kind::Mono);
    bold_ = load(Kind::SansBold);
    synthetic_bold_ = !bold_;
    if (!regular_) {
        if (error) {
            *error = L"No usable system font was found. The launcher needs one; on Linux "
                     L"install fonts-dejavu or fonts-liberation.";
        }
        return false;
    }
    return true;
}

void TextEngine::set_scale(float scale) {
    if (std::abs(scale - scale_) < 0.001f) return;
    scale_ = scale;
    if (cache_) cache_->glyphs.clear();
}

TextEngine::Font* TextEngine::slot(Face face) {
    FaceSpec face_spec = spec(face);
    Font* font = regular_.get();
    if (face_spec.mono && mono_) {
        font = mono_.get();
    } else if (face_spec.bold && bold_) {
        font = bold_.get();
    }
    if (font) font->size = face_spec.size;
    return font;
}

const TextEngine::Font* TextEngine::slot(Face face) const {
    return const_cast<TextEngine*>(this)->slot(face);
}

float TextEngine::line_height(Face face) const {
    const Font* font = slot(face);
    if (!font) return 0;
    float scale = stbtt_ScaleForPixelHeight(&font->info, font->size * scale_);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
    return static_cast<float>(ascent - descent + gap) * scale / scale_;
}

float TextEngine::ascent(Face face) const {
    const Font* font = slot(face);
    if (!font) return 0;
    float scale = stbtt_ScaleForPixelHeight(&font->info, font->size * scale_);
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &gap);
    return static_cast<float>(ascent) * scale / scale_;
}

float TextEngine::measure(wstring_view text, Face face) const {
    const Font* font = slot(face);
    if (!font || text.empty()) return 0;
    float scale = stbtt_ScaleForPixelHeight(&font->info, font->size * scale_);
    float width = 0;
    int previous = 0;
    for (uint32_t cp : codepoints(text)) {
        if (!stbtt_FindGlyphIndex(&font->info, static_cast<int>(cp))) cp = '?';
        if (previous) {
            width += static_cast<float>(stbtt_GetCodepointKernAdvance(
                         &font->info, previous, static_cast<int>(cp))) * scale;
        }
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&font->info, static_cast<int>(cp), &advance, &bearing);
        width += static_cast<float>(advance) * scale;
        previous = static_cast<int>(cp);
    }
    return width / scale_;
}

std::vector<wstring> TextEngine::wrap(wstring_view text, Face face, float max_width,
                                      bool wrap_lines) const {
    std::vector<wstring> lines;
    if (text.empty()) return lines;

    std::vector<wstring> paragraphs;
    wstring current;
    for (wchar_t character : text) {
        if (character == L'\n') {
            paragraphs.push_back(current);
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    paragraphs.push_back(current);

    for (const auto& paragraph : paragraphs) {
        if (!wrap_lines || max_width <= 0) {
            lines.push_back(paragraph);
            continue;
        }
        auto words = split(paragraph, L' ');
        wstring line;
        for (const auto& word : words) {
            wstring candidate = line.empty() ? word : line + L" " + word;
            if (!line.empty() && measure(candidate, face) > max_width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        lines.push_back(line);
    }
    return lines;
}

const TextEngine::Glyph& TextEngine::glyph(Face face, uint32_t codepoint, float subpixel_x,
                                           float subpixel_y) {
    Font* font = slot(face);
    int quantised_x = static_cast<int>(std::lround(subpixel_x * 4.0f)) & 3;
    int quantised_y = static_cast<int>(std::lround(subpixel_y * 4.0f)) & 3;
    std::uint64_t key = (static_cast<std::uint64_t>(static_cast<int>(face)) << 32) |
                        (static_cast<std::uint64_t>(codepoint) << 8) |
                        static_cast<std::uint64_t>(quantised_x * 4 + quantised_y);
    auto found = cache_->glyphs.find(key);
    if (found != cache_->glyphs.end()) return found->second;

    Glyph glyph;
    float scale = stbtt_ScaleForPixelHeight(&font->info, font->size * scale_);
    int width = 0, height = 0, offset_x = 0, offset_y = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmapSubpixel(
        &font->info, scale, scale, static_cast<float>(quantised_x) / 4.0f,
        static_cast<float>(quantised_y) / 4.0f, static_cast<int>(codepoint), &width, &height,
        &offset_x, &offset_y);
    glyph.width = width;
    glyph.height = height;
    glyph.offset_x = offset_x;
    glyph.offset_y = offset_y;
    if (bitmap && width > 0 && height > 0) {
        glyph.coverage.assign(bitmap, bitmap + static_cast<size_t>(width) * height);
    }
    if (bitmap) stbtt_FreeBitmap(bitmap, nullptr);
    return cache_->glyphs.emplace(key, std::move(glyph)).first->second;
}

void TextEngine::blend(std::uint32_t* pixels, int pixel_width, int pixel_height, RectF clip,
                       int x, int y, int w, int h, const unsigned char* coverage, Color color,
                       float strength) {
    int left = std::max(x, static_cast<int>(std::ceil(clip.left)));
    int top = std::max(y, static_cast<int>(std::ceil(clip.top)));
    int right = std::min(x + w, static_cast<int>(std::floor(clip.right)));
    int bottom = std::min(y + h, static_cast<int>(std::floor(clip.bottom)));
    left = std::max(left, 0);
    top = std::max(top, 0);
    right = std::min(right, pixel_width);
    bottom = std::min(bottom, pixel_height);
    for (int py = top; py < bottom; ++py) {
        std::uint32_t* row = pixels + static_cast<size_t>(py) * pixel_width;
        const unsigned char* source = coverage + static_cast<size_t>(py - y) * w;
        for (int px = left; px < right; ++px) {
            float value = static_cast<float>(source[px - x]) / 255.0f * strength;
            if (value <= 0.0f) continue;
            row[px] = blend_over(row[px], color, value);
        }
    }
}

void TextEngine::draw_line(std::uint32_t* pixels, int pixel_width, int pixel_height,
                           RectF clip, float x, float baseline, wstring_view text, Face face,
                           Color color) {
    Font* font = slot(face);
    if (!font || text.empty()) return;
    float scale = stbtt_ScaleForPixelHeight(&font->info, font->size * scale_);

    float pen = x * scale_;
    const float baseline_pixels = baseline * scale_;
    int previous = 0;
    const bool embolden = synthetic_bold_ && spec(face).bold;

    for (uint32_t cp : codepoints(text)) {
        if (!stbtt_FindGlyphIndex(&font->info, static_cast<int>(cp))) cp = '?';
        if (previous) {
            pen += static_cast<float>(stbtt_GetCodepointKernAdvance(
                       &font->info, previous, static_cast<int>(cp))) * scale;
        }
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&font->info, static_cast<int>(cp), &advance, &bearing);

        float cell = std::floor(pen);
        float baseline_cell = std::floor(baseline_pixels);
        const Glyph& bitmap = this->glyph(face, cp, pen - cell, baseline_pixels - baseline_cell);
        if (bitmap.width > 0 && bitmap.height > 0) {
            int left = static_cast<int>(cell) + bitmap.offset_x;
            int top = static_cast<int>(baseline_cell) + bitmap.offset_y;
            blend(pixels, pixel_width, pixel_height, clip, left, top, bitmap.width,
                  bitmap.height, bitmap.coverage.data(), color, 1.0f);
            if (embolden) {
                int extra = std::max(1, static_cast<int>(std::lround(scale * 0.6f)));
                blend(pixels, pixel_width, pixel_height, clip, left + extra, top,
                      bitmap.width, bitmap.height, bitmap.coverage.data(), color, 0.75f);
            }
        }
        pen += static_cast<float>(advance) * scale;
        previous = static_cast<int>(cp);
    }
}

}  // namespace nla
