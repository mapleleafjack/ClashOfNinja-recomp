#pragma once

#include <functional>
#include <string>

#include "picker.h"
#include "screen.h"

namespace nla {

// Asking the player for a folder or a file: the platform's own chooser where there is one,
// and the launcher's own list where there is not, or where a render has no window to show
// one over.
//
// The answer is a callback rather than a return value, because a native chooser answers
// whenever the player gets to it. What that callback does -- set a project up, run the
// extractor, take a patch -- belongs to the caller, so this page only carries it and hands
// it back.
class PickerScreen : public Screen {
  public:
    explicit PickerScreen(ScreenHost& host) : host_(host) {}

    // Asks for a folder, a disc image or a patch file, starting where the last one left off
    // or at `start`. `accept` runs on the UI thread with the answer.
    void choose_path(PickerKind kind, const std::wstring& title, const fs::path& start,
                     std::function<void(const fs::path&)> accept);

    // The player backed out: nothing is answered, and a launch that was waiting on the
    // answer is given up with it.
    void cancel();
    void close();

    bool capturing() const override { return chooser_.is_open(); }
    bool key(ScreenKey key) override;
    bool text(const std::wstring& typed) override;
    bool wheel(int notches) override;

    // The panel as it is drawn over the page: the scrim, the field, the list and the two
    // buttons, with the hit list the window clicks against.
    void paint(const PaintContext& ctx);

    bool handles(Id id) const override;
    void activate(Id id) override;

    // The list, for the drawing.
    Picker& chooser() { return chooser_; }
    const Picker& chooser() const { return chooser_; }

  private:
    void open();
    void accept();
    void list();
    void descend(const fs::path& folder);

    ScreenHost& host_;
    Picker chooser_;
};

}  // namespace nla
