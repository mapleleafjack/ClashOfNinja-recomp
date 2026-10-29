#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nla {

using std::wstring;
using std::wstring_view;

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};

Color rgb(unsigned hex, float alpha = 1.0f);

namespace theme {
constexpr unsigned Background = 0x12141a;
constexpr unsigned Card = 0x1b1e25;
constexpr unsigned CardHover = 0x20252e;
constexpr unsigned Border = 0x272c36;
constexpr unsigned BorderStrong = 0x333a46;
constexpr unsigned Accent = 0xe2693a;
constexpr unsigned AccentHover = 0xee7a4b;
constexpr unsigned Text = 0xe7e9ef;
constexpr unsigned Muted = 0x98a1b2;
constexpr unsigned Ok = 0x4ec98a;
constexpr unsigned Warn = 0xe8b64c;
constexpr unsigned Bad = 0xf0705c;
constexpr unsigned Link = 0x8fb4ff;
constexpr unsigned Log = 0x0d0f13;
}  // namespace theme

enum class Face {
    Title,
    Hero,
    Subtitle,
    CardTitle,
    Body,
    Small,
    Tiny,
    Mono,
    Button,
    ButtonLarge,
};

struct RectF {
    float left = 0, top = 0, right = 0, bottom = 0;

    RectF() = default;
    RectF(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}

    float width() const { return right - left; }
    float height() const { return bottom - top; }
};

enum class TextAlign { Leading, Center, Trailing };
enum class TextVAlign { Near, Center, Far };

// Source-over in the 0xAARRGGBB buffers this launcher draws into.
std::uint32_t blend_over(std::uint32_t destination, Color color, float coverage);

class TextEngine;

// Draws into a CPU pixel buffer that the window presents. Everything is measured in
// logical units and multiplied by the display scale, so the layout is identical at 1×
// and 2×, and every platform renders the same pixels.
class Painter {
  public:
    using Present = std::function<void(const std::uint32_t*, int, int)>;

    Painter();
    ~Painter();
    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;

    bool initialise(Present present, wstring* error);
    void resize(int pixel_width, int pixel_height, float scale);
    float scale() const { return scale_; }

    bool begin();
    void end();

    void clear(Color color);
    void fill_rect(const RectF& rect, Color color);
    void fill_round(const RectF& rect, float radius, Color color);
    void stroke_round(const RectF& rect, float radius, Color color, float width = 1.0f);
    void line(float x1, float y1, float x2, float y2, Color color, float width = 1.0f);
    void circle(float x, float y, float radius, Color color);

    void text(wstring_view text, const RectF& rect, Face face, Color color,
              TextAlign align = TextAlign::Leading, TextVAlign valign = TextVAlign::Near,
              bool wrap = true);
    void text_clipped(wstring_view text, const RectF& rect, Face face, Color color,
                      TextAlign align = TextAlign::Leading);
    float measure(wstring_view text, Face face, float max_width = 10000.0f);
    // How tall one line of `face` is, in logical units, for stacking lines of text.
    float line_height(Face face);

    int pixel_width() const { return width_; }
    int pixel_height() const { return height_; }
    const std::uint32_t* buffer() const { return pixels_.data(); }

    // The mark next to the title, embedded in the executable.
    bool has_logo() const { return !logo_.empty(); }
    void draw_logo(const RectF& rect);

    // Draws 0xAARRGGBB pixels into `rect`, scaled to fill it and cropping what does not
    // fit, so an image keeps its proportions. `radius` rounds the corners, `opacity`
    // fades the whole image over what is underneath.
    void draw_image(const RectF& rect, const std::uint32_t* pixels, int width, int height,
                    float radius = 0.0f, float opacity = 1.0f);

  private:
    std::unique_ptr<TextEngine> text_;
    Present present_;
    std::vector<std::uint32_t> pixels_;
    std::vector<std::uint32_t> logo_;
    int logo_width_ = 0;
    int logo_height_ = 0;
    int width_ = 0;
    int height_ = 0;
    float scale_ = 1.0f;
};

}  // namespace nla
