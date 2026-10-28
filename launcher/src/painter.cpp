#include "painter.h"

#include "font.h"
#include "image.h"
#include "util.h"

#include <algorithm>
#include <cmath>

#if defined(__has_include)
#if __has_include("logo_data.h")
#include "logo_data.h"
#define NLA_HAS_LOGO 1
#endif
#endif

namespace nla {

Color rgb(unsigned hex, float alpha) {
    Color color;
    color.r = static_cast<float>((hex >> 16) & 0xFF) / 255.0f;
    color.g = static_cast<float>((hex >> 8) & 0xFF) / 255.0f;
    color.b = static_cast<float>(hex & 0xFF) / 255.0f;
    color.a = alpha;
    return color;
}

std::uint32_t blend_over(std::uint32_t destination, Color color, float coverage) {
    float alpha = color.a * coverage;
    if (alpha <= 0.0f) return destination;
    if (alpha > 1.0f) alpha = 1.0f;
    auto channel = [alpha](float source, std::uint32_t target) {
        float value = source * 255.0f * alpha + static_cast<float>(target) * (1.0f - alpha);
        return static_cast<std::uint32_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
    };
    std::uint32_t red = channel(color.r, (destination >> 16) & 0xFF);
    std::uint32_t green = channel(color.g, (destination >> 8) & 0xFF);
    std::uint32_t blue = channel(color.b, destination & 0xFF);
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

namespace {

float rounded_rect_distance(float px, float py, const RectF& rect, float radius) {
    float half_w = rect.width() / 2.0f;
    float half_h = rect.height() / 2.0f;
    radius = std::clamp(radius, 0.0f, std::min(half_w, half_h));
    float qx = std::abs(px - (rect.left + half_w)) - (half_w - radius);
    float qy = std::abs(py - (rect.top + half_h)) - (half_h - radius);
    float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
    return outside + std::min(std::max(qx, qy), 0.0f) - radius;
}

float segment_distance(float px, float py, float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float length_squared = dx * dx + dy * dy;
    float t = 0.0f;
    if (length_squared > 0.0f) {
        t = std::clamp(((px - x1) * dx + (py - y1) * dy) / length_squared, 0.0f, 1.0f);
    }
    return std::hypot(px - (x1 + t * dx), py - (y1 + t * dy));
}

float coverage_for(float distance) { return std::clamp(0.5f - distance, 0.0f, 1.0f); }

}  // namespace

Painter::Painter() : text_(std::make_unique<TextEngine>()) {}
Painter::~Painter() = default;

bool Painter::initialise(Present present, wstring* error) {
    present_ = std::move(present);
#if defined(NLA_HAS_LOGO)
    decode_png(kLogoPng, kLogoPngSize, &logo_width_, &logo_height_, logo_);
#endif
    return text_->initialise(error);
}

void Painter::resize(int pixel_width, int pixel_height, float scale) {
    if (width_ == pixel_width && height_ == pixel_height && std::abs(scale - scale_) < 0.001f &&
        !pixels_.empty()) {
        return;
    }
    width_ = std::max(1, pixel_width);
    height_ = std::max(1, pixel_height);
    scale_ = scale > 0.0f ? scale : 1.0f;
    pixels_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0xFF000000u);
    text_->set_scale(scale_);
}

bool Painter::begin() { return !pixels_.empty(); }

void Painter::end() {
    if (present_ && !pixels_.empty()) present_(pixels_.data(), width_, height_);
}

void Painter::clear(Color color) {
    std::uint32_t packed = 0xFF000000u |
                           (static_cast<std::uint32_t>(std::lround(color.r * 255.0f)) << 16) |
                           (static_cast<std::uint32_t>(std::lround(color.g * 255.0f)) << 8) |
                           static_cast<std::uint32_t>(std::lround(color.b * 255.0f));
    std::fill(pixels_.begin(), pixels_.end(), packed);
}

void Painter::fill_rect(const RectF& rect, Color color) {
    int left = static_cast<int>(std::lround(rect.left * scale_));
    int top = static_cast<int>(std::lround(rect.top * scale_));
    int right = static_cast<int>(std::lround(rect.right * scale_));
    int bottom = static_cast<int>(std::lround(rect.bottom * scale_));
    left = std::clamp(left, 0, width_);
    right = std::clamp(right, 0, width_);
    top = std::clamp(top, 0, height_);
    bottom = std::clamp(bottom, 0, height_);
    for (int y = top; y < bottom; ++y) {
        std::uint32_t* row = pixels_.data() + static_cast<size_t>(y) * width_;
        for (int x = left; x < right; ++x) row[x] = blend_over(row[x], color, 1.0f);
    }
}

void Painter::fill_round(const RectF& rect, float radius, Color color) {
    RectF scaled{rect.left * scale_, rect.top * scale_, rect.right * scale_,
                 rect.bottom * scale_};
    float scaled_radius = radius * scale_;
    int left = std::clamp(static_cast<int>(std::floor(scaled.left)) - 1, 0, width_);
    int right = std::clamp(static_cast<int>(std::ceil(scaled.right)) + 1, 0, width_);
    int top = std::clamp(static_cast<int>(std::floor(scaled.top)) - 1, 0, height_);
    int bottom = std::clamp(static_cast<int>(std::ceil(scaled.bottom)) + 1, 0, height_);
    float centre_x = scaled.left + scaled.width() / 2.0f;
    float centre_y = scaled.top + scaled.height() / 2.0f;
    float inner_x = scaled.width() / 2.0f - scaled_radius;
    float inner_y = scaled.height() / 2.0f - scaled_radius;

    for (int y = top; y < bottom; ++y) {
        std::uint32_t* row = pixels_.data() + static_cast<size_t>(y) * width_;
        float py = static_cast<float>(y) + 0.5f;
        float qy = std::abs(py - centre_y) - inner_y;
        for (int x = left; x < right; ++x) {
            float qx = std::abs(static_cast<float>(x) + 0.5f - centre_x) - inner_x;
            // Inside the inner rectangle: fully covered, no distance needed. That is most
            // of a card, which is what keeps a 2x repaint cheap.
            if (qx <= 0.0f && qy <= 0.0f) {
                row[x] = blend_over(row[x], color, 1.0f);
                continue;
            }
            float coverage = coverage_for(rounded_rect_distance(
                static_cast<float>(x) + 0.5f, py, scaled, scaled_radius));
            if (coverage > 0.0f) row[x] = blend_over(row[x], color, coverage);
        }
    }
}

void Painter::stroke_round(const RectF& rect, float radius, Color color, float width) {
    RectF scaled{rect.left * scale_, rect.top * scale_, rect.right * scale_,
                 rect.bottom * scale_};
    float scaled_radius = radius * scale_;
    float half = std::max(0.5f, width * scale_ / 2.0f);
    int left = std::clamp(static_cast<int>(std::floor(scaled.left - half)) - 1, 0, width_);
    int right = std::clamp(static_cast<int>(std::ceil(scaled.right + half)) + 1, 0, width_);
    int top = std::clamp(static_cast<int>(std::floor(scaled.top - half)) - 1, 0, height_);
    int bottom = std::clamp(static_cast<int>(std::ceil(scaled.bottom + half)) + 1, 0,
                            height_);
    float centre_x = scaled.left + scaled.width() / 2.0f;
    float centre_y = scaled.top + scaled.height() / 2.0f;
    float inner_x = scaled.width() / 2.0f - scaled_radius;
    float inner_y = scaled.height() / 2.0f - scaled_radius;

    for (int y = top; y < bottom; ++y) {
        std::uint32_t* row = pixels_.data() + static_cast<size_t>(y) * width_;
        float py = static_cast<float>(y) + 0.5f;
        float qy = std::abs(py - centre_y) - inner_y;
        for (int x = left; x < right; ++x) {
            float qx = std::abs(static_cast<float>(x) + 0.5f - centre_x) - inner_x;
            // Deep inside the shape: no part of the border reaches this pixel.
            if (qx <= -half - 1.0f && qy <= -half - 1.0f) continue;
            float distance =
                std::abs(rounded_rect_distance(static_cast<float>(x) + 0.5f, py, scaled,
                                               scaled_radius)) -
                half;
            float coverage = coverage_for(distance);
            if (coverage > 0.0f) row[x] = blend_over(row[x], color, coverage);
        }
    }
}

void Painter::line(float x1, float y1, float x2, float y2, Color color, float width) {
    float ax = x1 * scale_, ay = y1 * scale_, bx = x2 * scale_, by = y2 * scale_;
    float half = std::max(0.5f, width * scale_ / 2.0f);

    // Axis-aligned lines snap to the pixel grid: a 1px divider stays one crisp pixel.
    if (std::abs(ay - by) < 0.01f) {
        float centre = std::round(ay);
        fill_rect(RectF{std::min(ax, bx) / scale_, (centre - half) / scale_,
                        std::max(ax, bx) / scale_, (centre + half) / scale_},
                  color);
        return;
    }
    if (std::abs(ax - bx) < 0.01f) {
        float centre = std::round(ax);
        fill_rect(RectF{(centre - half) / scale_, std::min(ay, by) / scale_,
                        (centre + half) / scale_, std::max(ay, by) / scale_},
                  color);
        return;
    }

    int left = std::clamp(static_cast<int>(std::floor(std::min(ax, bx) - half)) - 1, 0,
                          width_);
    int right = std::clamp(static_cast<int>(std::ceil(std::max(ax, bx) + half)) + 1, 0,
                           width_);
    int top = std::clamp(static_cast<int>(std::floor(std::min(ay, by) - half)) - 1, 0,
                         height_);
    int bottom = std::clamp(static_cast<int>(std::ceil(std::max(ay, by) + half)) + 1, 0,
                            height_);
    for (int y = top; y < bottom; ++y) {
        std::uint32_t* row = pixels_.data() + static_cast<size_t>(y) * width_;
        float py = static_cast<float>(y) + 0.5f;
        for (int x = left; x < right; ++x) {
            float px = static_cast<float>(x) + 0.5f;
            float distance = segment_distance(px, py, ax, ay, bx, by) - half;
            float coverage = coverage_for(distance);
            if (coverage > 0.0f) row[x] = blend_over(row[x], color, coverage);
        }
    }
}

void Painter::circle(float x, float y, float radius, Color color) {
    float cx = x * scale_, cy = y * scale_, r = radius * scale_;
    int left = std::clamp(static_cast<int>(std::floor(cx - r)) - 1, 0, width_);
    int right = std::clamp(static_cast<int>(std::ceil(cx + r)) + 1, 0, width_);
    int top = std::clamp(static_cast<int>(std::floor(cy - r)) - 1, 0, height_);
    int bottom = std::clamp(static_cast<int>(std::ceil(cy + r)) + 1, 0, height_);
    for (int py = top; py < bottom; ++py) {
        std::uint32_t* row = pixels_.data() + static_cast<size_t>(py) * width_;
        float dy = static_cast<float>(py) + 0.5f - cy;
        for (int px = left; px < right; ++px) {
            float dx = static_cast<float>(px) + 0.5f - cx;
            float coverage = coverage_for(std::hypot(dx, dy) - r);
            if (coverage > 0.0f) row[px] = blend_over(row[px], color, coverage);
        }
    }
}

void Painter::text(wstring_view content, const RectF& rect, Face face, Color color,
                   TextAlign align, TextVAlign valign, bool wrap) {
    if (content.empty() || pixels_.empty()) return;
    auto lines = text_->wrap(content, face, rect.width(), wrap);
    if (lines.empty()) return;
    float line_height = text_->line_height(face);
    float total = line_height * static_cast<float>(lines.size());
    float y = rect.top;
    if (valign == TextVAlign::Center) y += (rect.height() - total) / 2.0f;
    if (valign == TextVAlign::Far) y += rect.height() - total;

    RectF clip{rect.left * scale_ - 1.0f, rect.top * scale_ - 1.0f,
               rect.right * scale_ + 1.0f, rect.bottom * scale_ + 1.0f};
    for (const auto& line : lines) {
        float width = text_->measure(line, face);
        float x = rect.left;
        if (align == TextAlign::Center) x += (rect.width() - width) / 2.0f;
        if (align == TextAlign::Trailing) x += rect.width() - width;
        text_->draw_line(pixels_.data(), width_, height_, clip, x,
                         y + text_->ascent(face), line, face, color);
        y += line_height;
    }
}

void Painter::text_clipped(wstring_view content, const RectF& rect, Face face, Color color,
                           TextAlign align) {
    text(content, rect, face, color, align, TextVAlign::Center, false);
}

float Painter::measure(wstring_view content, Face face, float) {
    return text_->measure(content, face);
}

void Painter::draw_logo(const RectF& rect) {
    if (logo_.empty()) return;
    int left = static_cast<int>(std::lround(rect.left * scale_));
    int top = static_cast<int>(std::lround(rect.top * scale_));
    int width = static_cast<int>(std::lround(rect.width() * scale_));
    int height = static_cast<int>(std::lround(rect.height() * scale_));
    if (width <= 0 || height <= 0) return;

    for (int y = 0; y < height; ++y) {
        int py = top + y;
        if (py < 0 || py >= height_) continue;
        std::uint32_t* row = pixels_.data() + static_cast<size_t>(py) * width_;
        float source_y = (static_cast<float>(y) + 0.5f) * logo_height_ / height - 0.5f;
        int y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, logo_height_ - 1);
        int y1 = std::min(y0 + 1, logo_height_ - 1);
        float fy = std::clamp(source_y - y0, 0.0f, 1.0f);
        for (int x = 0; x < width; ++x) {
            int px = left + x;
            if (px < 0 || px >= width_) continue;
            float source_x = (static_cast<float>(x) + 0.5f) * logo_width_ / width - 0.5f;
            int x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, logo_width_ - 1);
            int x1 = std::min(x0 + 1, logo_width_ - 1);
            float fx = std::clamp(source_x - x0, 0.0f, 1.0f);

            std::uint32_t texels[4] = {
                logo_[static_cast<size_t>(y0) * logo_width_ + x0],
                logo_[static_cast<size_t>(y0) * logo_width_ + x1],
                logo_[static_cast<size_t>(y1) * logo_width_ + x0],
                logo_[static_cast<size_t>(y1) * logo_width_ + x1],
            };
            float weights[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
            Color color{0, 0, 0, 0};
            for (int corner = 0; corner < 4; ++corner) {
                std::uint32_t texel = texels[corner];
                color.r += static_cast<float>((texel >> 16) & 0xFF) / 255.0f * weights[corner];
                color.g += static_cast<float>((texel >> 8) & 0xFF) / 255.0f * weights[corner];
                color.b += static_cast<float>(texel & 0xFF) / 255.0f * weights[corner];
                color.a += static_cast<float>((texel >> 24) & 0xFF) / 255.0f * weights[corner];
            }
            if (color.a > 0.0f) row[px] = blend_over(row[px], color, 1.0f);
        }
    }
}

}  // namespace nla
