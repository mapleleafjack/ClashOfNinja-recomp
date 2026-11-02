#include "activity_panel.h"

#include <algorithm>

namespace nla {

void ActivityPanel::begin() {
    reset();
    visible_ = true;
    offset_ = 0;
}

void ActivityPanel::reset() { activity_.reset(); }

void ActivityPanel::write(const std::wstring& text) { activity_.write(text); }

void ActivityPanel::clear() {
    activity_.clear();
    offset_ = 0;
}

void ActivityPanel::scroll(int notches) {
    offset_ = std::max(0, offset_ + notches * kLinesPerNotch);
}

void ActivityPanel::paint_bar(const PaintContext& ctx, const Rect& bar) const {
    ctx.painter.fill_round(bar.box(), 3, rgb(0x242932));
    if (percent() >= 0) {
        float fraction = std::min(1.0f, static_cast<float>(percent()) / 100.0f);
        ctx.painter.fill_round(
            RectF(bar.x, bar.y, bar.x + bar.w * fraction, bar.y + bar.h), 3,
            rgb(theme::Accent));
        return;
    }
    float chunk = bar.w * 0.28f;
    float travel = bar.w + chunk;
    float start = bar.x - chunk + travel * phase_;
    float left = std::max(bar.x, start);
    float right_edge = std::min(bar.x + bar.w, start + chunk);
    if (right_edge > left) {
        ctx.painter.fill_round(RectF(left, bar.y, right_edge, bar.y + bar.h), 3,
                               rgb(theme::Accent));
    }
}

bool ActivityPanel::advance() {
    phase_ += 0.025f;
    if (phase_ > 1.0f) phase_ -= 1.0f;
    return true;
}

void ActivityPanel::clamp_offset(int visible_rows) {
    const int total = static_cast<int>(activity_.lines().size());
    offset_ = std::min(offset_, std::max(0, total - std::max(1, visible_rows)));
}

void ActivityPanel::paint(const PaintContext& ctx, float top, bool busy,
                          bool cancellable) {
    float card_height = std::max(140.0f, ctx.height - top - 24.0f);
    Rect card{24, top, ctx.width - 48, card_height};
    ctx.ui.card_box(card);

    ctx.painter.text(L"Activity", RectF(card.x + 20, card.y + 16, card.x + 200,
                                           card.y + 40),
                  Face::CardTitle, rgb(theme::Text));

    std::wstring toggle = visible() ? L"Hide" : L"Show";
    float clear_w = ctx.ui.text_width(L"Clear", Face::Tiny);
    float copy_w = ctx.ui.text_width(L"Copy", Face::Tiny);
    float toggle_w = ctx.ui.text_width(toggle, Face::Tiny);
    float right = card.x + card.w - 20;
    ctx.ui.link(Id::LogToggle, Rect{right - toggle_w, card.y + 18, toggle_w, 18}, toggle);
    right -= toggle_w + 16;
    ctx.ui.link(Id::CopyLog, Rect{right - copy_w, card.y + 18, copy_w, 18}, L"Copy");
    right -= copy_w + 16;
    ctx.ui.link(Id::ClearLog, Rect{right - clear_w, card.y + 18, clear_w, 18}, L"Clear");

    float content_top = card.y + 44;
    float content_bottom = card.y + card.h - 14;
    float reserved = busy ? 34.0f : 0.0f;

    set_rect({});
    if (visible()) {
        Rect log_area{card.x + 20, content_top, card.w - 40,
                      std::max(40.0f, content_bottom - reserved - content_top)};
        set_rect(log_area);
        ctx.painter.fill_round(log_area.box(), 10, rgb(theme::Log));
        ctx.painter.stroke_round(log_area.box(), 10, rgb(0x242932), 1);
        if (empty()) {
            ctx.painter.text(L"Setup and build output appears here.",
                          RectF(log_area.x + 12, log_area.y + 8, log_area.x + log_area.w - 12,
                                      log_area.y + 28),
                          Face::Mono, rgb(0x646d7d));
        } else {
            const float line_height = ctx.painter.line_height(Face::Mono);
            int visible = static_cast<int>((log_area.h - 16) / line_height);
            visible = std::max(1, visible);
            int total = static_cast<int>(lines().size());
            clamp_offset(visible);
            int end = total - offset();
            int start = std::max(0, end - visible);
            for (int index = start; index < end; ++index) {
                float y = log_area.y + 8 + (index - start) * line_height;
                Color color = rgb(0xc8cede);
                const auto& line = lines()[static_cast<size_t>(index)];
                if (line.rfind(L"$ ", 0) == 0) color = rgb(0xe0a084);
                if (line.rfind(L"[launcher]", 0) == 0) color = rgb(theme::Muted);
                ctx.painter.text_clipped(line,
                                      RectF(log_area.x + 12, y, log_area.x + log_area.w - 12,
                                                  y + line_height + 2),
                                      Face::Mono, color);
            }
        }
    }

    if (busy) {
        float bottom = card.y + card.h - 14;
        float bar_y = bottom - 8;
        std::wstring label = stage().empty() ? L"Working…" : stage();
        ctx.painter.text(label, RectF(card.x + 20, bottom - 30, card.x + card.w - 160,
                                         bottom - 10),
                      Face::Tiny, rgb(theme::Muted));
        Rect cancel{card.x + card.w - 20 - 86, bottom - 32, 86, 24};
        if (cancellable) {
            ctx.painter.text(L"Cancel", RectF(cancel.x, cancel.y, cancel.x + cancel.w,
                                                 cancel.y + cancel.h),
                          Face::Tiny, rgb(theme::Link), TextAlign::Center,
                          TextVAlign::Center);
            ctx.ui.register_hit(Id::Stop, cancel);
        }

        paint_bar(ctx, Rect{card.x + 20, bar_y, card.w - 40, 6});
    }
}

}  // namespace nla
