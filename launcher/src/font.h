#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "painter.h"

namespace nla {

// Rasterises text with stb_truetype, so every platform lays out and draws text the
// same way and the launcher needs no system text stack.
class TextEngine {
  public:
    TextEngine();
    ~TextEngine();
    TextEngine(const TextEngine&) = delete;
    TextEngine& operator=(const TextEngine&) = delete;

    bool initialise(wstring* error);
    // Logical units per point; glyphs are rasterised at size * scale.
    void set_scale(float scale);
    float scale() const { return scale_; }

    float line_height(Face face) const;
    float ascent(Face face) const;
    float measure(wstring_view text, Face face) const;

    // Breaks `text` into lines that fit `max_width` logical units. Without wrapping the
    // text is only split on newlines.
    std::vector<wstring> wrap(wstring_view text, Face face, float max_width,
                              bool wrap) const;

    // Draws one line with its left edge at `x` and its baseline at `baseline`, both in
    // logical units, clipped to `clip` (pixels).
    void draw_line(std::uint32_t* pixels, int pixel_width, int pixel_height, RectF clip,
                   float x, float baseline, wstring_view text, Face face, Color color);

  private:
    struct Font;
    struct Glyph;

    Font* slot(Face face);
    const Font* slot(Face face) const;
    const Glyph& glyph(Face face, uint32_t codepoint, float subpixel_x, float subpixel_y);
    void blend(std::uint32_t* pixels, int pixel_width, int pixel_height, RectF clip, int x,
               int y, int w, int h, const unsigned char* coverage, Color color,
               float strength);

    std::unique_ptr<Font> regular_;
    std::unique_ptr<Font> bold_;
    std::unique_ptr<Font> mono_;
    bool synthetic_bold_ = false;
    float scale_ = 1.0f;
    struct Cache;
    std::unique_ptr<Cache> cache_;
};

}  // namespace nla
