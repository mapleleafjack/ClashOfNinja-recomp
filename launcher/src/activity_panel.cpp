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

void ActivityPanel::clamp_offset(int visible_rows) {
    const int total = static_cast<int>(activity_.lines().size());
    offset_ = std::min(offset_, std::max(0, total - std::max(1, visible_rows)));
}

}  // namespace nla
