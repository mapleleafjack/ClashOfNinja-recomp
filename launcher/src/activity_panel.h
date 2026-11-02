#pragma once

#include <deque>
#include <string>

#include "jobs.h"
#include "ui.h"

namespace nla {

// The Activity panel: the lines a job reports, and where the page put them. It holds no
// window and no painter, so what a run leaves in it, and what the wheel does to it, can be
// checked without either.
//
// The four pieces of view state here -- open or folded, how far back the lines are scrolled,
// where the panel was drawn, and whether the mouse is over it -- were fields on App, and each
// one was asked about from the event loop and from the drawing as if it were the log's own
// business. They are: the panel is the only thing that shows them.
class ActivityPanel {
  public:
    // A new job: its progress starts again, and the panel opens to show the output. The
    // lines the last steps printed are kept, because they are still what went wrong.
    void begin();
    // The job is over: its lines and its progress go, so the next report starts clean. The
    // panel stays where it is, because the last lines are still worth reading.
    void reset();
    // The job finished with nothing to read, so the page goes back to its quiet shape.
    void fold() { visible_ = false; }

    void write(const std::wstring& text);
    void clear();

    const std::deque<std::wstring>& lines() const { return activity_.lines(); }
    bool empty() const { return activity_.empty(); }
    // Every line, for the clipboard.
    std::wstring text() const { return activity_.text(); }
    // -1 when the job has not said how far it is.
    int percent() const { return activity_.percent(); }
    void set_percent(int percent) { activity_.set_percent(percent); }
    const std::wstring& stage() const { return activity_.stage(); }
    void set_stage(const std::wstring& text) { activity_.set_stage(text); }

    bool visible() const { return visible_; }
    void toggle() { visible_ = !visible_; }

    // How far back the lines are scrolled, counted from the newest end.
    int offset() const { return offset_; }
    // The wheel: three lines a notch, the same as the other lists in the launcher.
    void scroll(int notches);
    // A frame with this many rows of room leaves the offset no further back than the first
    // line, so scrolling to the end of a short log cannot hide it.
    void clamp_offset(int visible_rows);

    // Where the lines were drawn last frame, so the wheel knows it is over them.
    const Rect& rect() const { return rect_; }
    void set_rect(const Rect& rect) { rect_ = rect; }
    bool hovered() const { return hovered_; }
    void set_hovered(bool hovered) { hovered_ = hovered; }

  private:
    static constexpr int kLinesPerNotch = 3;

    Activity activity_;
    bool visible_ = false;
    int offset_ = 0;
    Rect rect_{};
    bool hovered_ = false;
};

}  // namespace nla
