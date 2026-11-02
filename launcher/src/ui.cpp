#include "ui.h"

#include <algorithm>

namespace nla {

void UiContext::begin_frame() { hits_.clear(); }

std::optional<Rect> UiContext::rect_of(Id id) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->id == id) return it->rect;
    }
    return std::nullopt;
}

void UiContext::button(Id id, const Rect& rect, const std::wstring& label, bool primary,
                       bool enabled, bool danger, Face face,
                       const std::function<void(const Rect&, Color)>& mark) {
    bool hovered = enabled && hover_ == id;
    bool down = enabled && pressed_ == id;
    Color background;
    Color border;
    Color text;
    if (primary) {
        unsigned base = danger ? 0xa8402c : theme::Accent;
        unsigned hover = danger ? 0xbb4b35 : theme::AccentHover;
        background = enabled ? rgb(hovered ? hover : base) : rgb(0x513427);
        border = background;
        text = enabled ? rgb(0xffffff) : rgb(0xa08a7c);
    } else {
        background = !enabled ? rgb(0x1a1d23)
                              : rgb(hovered ? (down ? 0x20252d : 0x2c333e) : 0x242932);
        border = !enabled ? rgb(0x242932) : rgb(theme::BorderStrong);
        text = !enabled ? rgb(0x69707d) : (danger ? rgb(0xff9a86) : rgb(theme::Text));
    }
    painter_.fill_round(rect.box(), 8, background);
    painter_.stroke_round(rect.box(), 8, border, 1);
    if (mark) {
        mark(rect, text);
    } else {
        painter_.text_clipped(elide(label, face, rect.w - 12.0f),
                              RectF(rect.x + 6, rect.y, rect.x + rect.w - 6, rect.y + rect.h),
                              face, text, TextAlign::Center);
    }
    if (enabled) register_hit(id, rect);
}

void UiContext::link(Id id, const Rect& rect, const std::wstring& label, bool enabled) {
    bool hovered = enabled && hover_ == id;
    bool down = enabled && pressed_ == id;
    Color text = enabled ? rgb(hovered ? (down ? 0x8fb4ff : 0xb6cdff) : theme::Link)
                         : rgb(0x69707d);
    painter_.text_clipped(elide(label, Face::Tiny, rect.w),
                          RectF(rect.x, rect.y, rect.x + rect.w, rect.y + rect.h), Face::Tiny,
                          text, TextAlign::Center);
    if (enabled) register_hit(id, rect);
}

void UiContext::checkbox(Id id, float x, float y, const std::wstring& label, bool checked,
                         bool enabled) {
    Rect box{x, y, 17, 17};
    Color border = enabled ? (checked ? rgb(theme::Accent) : rgb(theme::BorderStrong))
                           : rgb(0x2c313a);
    if (checked) {
        painter_.fill_round(box.box(), 4, enabled ? rgb(theme::Accent) : rgb(0x513427));
    } else {
        painter_.fill_round(box.box(), 4, rgb(0x1b1e25));
    }
    painter_.stroke_round(box.box(), 4, border, 1);
    if (checked) {
        painter_.line(box.x + 4, box.y + 8.5f, box.x + 7, box.y + 12, rgb(0xffffff), 1.8f);
        painter_.line(box.x + 7, box.y + 12, box.x + 13, box.y + 5, rgb(0xffffff), 1.8f);
    }
    float label_width = text_width(label, Face::Small);
    painter_.text(label, RectF(x + 24, y - 2, x + 24 + label_width + 8, y + 20), Face::Small,
                  enabled ? rgb(theme::Text) : rgb(0x69707d));
    if (enabled) register_hit(id, Rect{x, y - 4, 24 + label_width + 4, 25});
}

void UiContext::combo(Id id, const Rect& rect, const std::wstring& value, bool enabled,
                      bool muted) {
    bool hovered = enabled && hover_ == id;
    painter_.fill_round(rect.box(), 8, rgb(hovered ? 0x2c333e : 0x242932));
    painter_.stroke_round(rect.box(), 8, rgb(theme::BorderStrong), 1);
    painter_.text(value, RectF(rect.x + 10, rect.y, rect.x + rect.w - 26, rect.y + rect.h),
                  Face::Small, enabled ? rgb(muted ? theme::Muted : theme::Text) : rgb(0x69707d),
                  TextAlign::Leading, TextVAlign::Center, false);
    float chevron_x = rect.x + rect.w - 16;
    float chevron_y = rect.y + rect.h / 2 - 2;
    painter_.line(chevron_x - 4, chevron_y, chevron_x, chevron_y + 4, rgb(theme::Muted), 1.6f);
    painter_.line(chevron_x, chevron_y + 4, chevron_x + 4, chevron_y, rgb(theme::Muted), 1.6f);
    if (enabled) register_hit(id, rect);
}

void UiContext::pill(const Rect& rect, const std::wstring& text, Pill state) {
    Color dot = rgb(theme::Muted);
    switch (state) {
        case Pill::Ok: dot = rgb(theme::Ok); break;
        case Pill::Warn: dot = rgb(theme::Warn); break;
        case Pill::Bad: dot = rgb(theme::Bad); break;
        case Pill::Muted: break;
    }
    painter_.circle(rect.x + 7, rect.y + rect.h / 2, 3.5f, dot);
    painter_.text(text, RectF(rect.x + 16, rect.y, rect.x + rect.w, rect.y + rect.h),
                  Face::Small, rgb(0xcfd5e2), TextAlign::Leading, TextVAlign::Center, false);
}

void UiContext::card_box(const Rect& rect) {
    painter_.fill_round(rect.box(), 14, rgb(theme::Card));
    painter_.stroke_round(rect.box(), 14, rgb(theme::Border), 1);
}

float UiContext::text_width(const std::wstring& text, Face face) {
    return painter_.measure(text, face);
}

std::wstring UiContext::elide(const std::wstring& text, Face face, float width) {
    if (width <= 0) return L"";
    if (text_width(text, face) <= width) return text;
    std::wstring suffix = L"…";
    size_t low = 0, high = text.size();
    while (low < high) {
        size_t mid = (low + high + 1) / 2;
        if (text_width(text.substr(0, mid) + suffix, face) <= width) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return text.substr(0, low) + suffix;
}

// A path keeps both ends: the folder it is in and the name that is being replaced. Cut
// short from the end alone it would read "~/Documents/Projects/Fun st…", which says
// nothing about what is being replaced.
std::wstring UiContext::elide_middle(const std::wstring& text, Face face, float width) {
    if (width <= 0) return L"";
    if (text_width(text, face) <= width) return text;
    const std::wstring mark = L"…";
    size_t keep = text.size() / 2;
    while (keep > 0) {
        const size_t tail = std::max<size_t>(1, keep / 2);
        const std::wstring candidate =
            text.substr(0, keep) + mark + text.substr(text.size() - tail);
        if (text_width(candidate, face) <= width) return candidate;
        --keep;
    }
    return mark + text.substr(text.size() - std::min<size_t>(text.size(), 3));
}

void UiContext::register_hit(Id id, const Rect& rect) { hits_.push_back({rect, id}); }

Id UiContext::hit_at(float x, float y) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->rect.contains(x, y)) return it->id;
    }
    return Id::None;
}

}  // namespace nla
