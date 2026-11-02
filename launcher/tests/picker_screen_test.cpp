// What asking the player for a folder or a file does: which keys the list takes, what a
// typed path appends to, which row a click descends into, and what is handed back when the
// answer is accepted or given up. All of it used to be in App's own key and click switches,
// reachable only through a window. Run by ctest; needs no window and opens no chooser.

#include "picker_screen.h"

#include "util.h"

#include <cstdio>
#include <string>
#include <vector>

namespace fs = nla::fs;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

// The host, recording what the page asked for. `native_chooser` always answers "no chooser
// here", which is the render's answer and the deterministic one.
struct RecordingHost {
    int redraws = 0;
    int takes_over = 0;
    int cancelled = 0;
    std::vector<bool> text_input;
    std::vector<std::wstring> logged;
    std::wstring clipboard_text;
    nla::NativeAnswer native;
    nla::ScreenHost host;

    RecordingHost() {
        host.invalidate = [this] { ++redraws; };
        host.post = [](std::function<void()>) {};
        host.log = [this](const std::wstring& text) { logged.push_back(text); };
        host.message = [](const std::wstring&, const std::wstring&) {};
        host.update_prompt = [](const std::wstring&, const std::wstring&, const std::wstring&,
                                std::function<void()>) {};
        host.busy = [] { return false; };
        host.game_running = [] { return false; };
        host.settings_note = [](const std::wstring&) {};
        host.launch_cancelled = [this] { ++cancelled; };
        host.start_job = [](std::vector<nla::Task>, nla::JobOptions) {};
        host.elide_path = [](const std::wstring& text) { return text; };
        host.clipboard = [this] { return clipboard_text; };
        host.text_input = [this](bool on) { text_input.push_back(on); };
        host.take_over = [this] { ++takes_over; };
        host.native_chooser = [this](nla::PickerKind, const std::wstring&, const fs::path&) {
            return native;
        };
    }
};

// A folder with the two shapes the picker sorts differently: folders first, then the files
// this kind offers, each by name.
fs::path make_folder() {
    const fs::path root = nla::temp_dir() / L"nla-picker-screen-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / L"alpha", ec);
    fs::create_directories(root / L"beta", ec);
    nla::write_text_file(root / L"disc.iso", "not read, only named\n");
    nla::write_text_file(root / L"note.txt", "not a disc\n");
    return root;
}

int row_of(const nla::PickerScreen& screen, const std::wstring& name) {
    const auto& entries = screen.chooser().entries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[static_cast<size_t>(i)].name == name) return i;
    }
    return -1;
}

// Opening the page closes whatever was over the main page and starts taking typed text.
void opening() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    const fs::path root = make_folder();
    int answered = 0;
    screen.choose_path(nla::PickerKind::Image, L"Choose your own disc image", root,
                       [&](const fs::path&) { ++answered; });

    check(screen.capturing(), "the page takes the keyboard while it is open");
    check(h.takes_over == 1, "and closes what was under it");
    check(!h.text_input.empty() && h.text_input.back(), "and the window takes typed text");
    check(answered == 0, "nothing is answered until the player answers");
    check(row_of(screen, L"alpha") >= 0 && row_of(screen, L"disc.iso") >= 0,
          "the folder it opened on is listed");

    // The parent first, then the folders, then the files this kind offers.
    const auto& entries = screen.chooser().entries();
    check(!entries.empty() && entries.front().name == L"..", "the parent folder comes first");
    check(row_of(screen, L"alpha") < row_of(screen, L"beta"), "folders are sorted by name");
    check(row_of(screen, L"beta") < row_of(screen, L"disc.iso"),
          "the files come after the folders");
    check(row_of(screen, L"note.txt") < 0, "a file this kind does not offer is not listed");
}

// Clicking a folder row descends into it; the up row comes back.
void descending() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    const fs::path root = make_folder();
    screen.choose_path(nla::PickerKind::Folder, L"Choose a folder", root, {});

    const int alpha = row_of(screen, L"alpha");
    screen.activate(static_cast<nla::Id>(static_cast<int>(nla::Id::PickerEntry) + alpha));
    check(screen.chooser().folder() == (root / L"alpha").wstring(),
          "the folder row descends into that folder");

    screen.activate(nla::Id::PickerUp);
    check(screen.chooser().folder() == root.wstring(), "the up row comes back to the parent");
    check(screen.capturing(), "and the page is still open");
}

// Clicking a file row fills the path in without answering: the player still presses Choose.
void picking_a_file() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    const fs::path root = make_folder();
    fs::path answer;
    screen.choose_path(nla::PickerKind::Image, L"Choose your own disc image", root,
                       [&](const fs::path& chosen) { answer = chosen; });

    const int disc = row_of(screen, L"disc.iso");
    screen.activate(static_cast<nla::Id>(static_cast<int>(nla::Id::PickerEntry) + disc));
    check(screen.chooser().path() == (root / L"disc.iso").wstring(),
          "the file row puts its path in the field");
    check(screen.chooser().editing(), "and the field takes the keyboard");
    check(answer.empty(), "picking a row is not answering yet");

    screen.key(nla::ScreenKey::Enter);
    check(answer == root / L"disc.iso", "Enter answers with the path in the field");
    check(!screen.capturing(), "and the page closes");
    check(!h.text_input.empty() && !h.text_input.back(), "and the window stops taking text");
}

// A folder that is not there is a note, not an answer, and not a dismissal.
void a_missing_folder() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    const fs::path root = make_folder();
    int answered = 0;
    screen.choose_path(nla::PickerKind::Folder, L"Choose a folder", root,
                       [&](const fs::path&) { ++answered; });

    screen.chooser().path() = (root / L"nowhere").wstring();
    screen.key(nla::ScreenKey::Enter);
    check(answered == 0, "a folder that is not there is not an answer");
    check(screen.capturing(), "the page stays open");
    check(screen.chooser().note() == L"That folder does not exist.", "and says why");
}

// Typing, backspace and paste only change the field while it has the keyboard.
void typing() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    const fs::path root = make_folder();
    screen.choose_path(nla::PickerKind::Folder, L"Choose a folder", root, {});

    screen.key(nla::ScreenKey::Tab);
    check(screen.chooser().editing(), "Tab puts the field in front");

    const std::wstring before = screen.chooser().path();
    screen.text(L"abc");
    check(screen.chooser().path() == before + L"abc", "typed text is appended to the path");

    screen.key(nla::ScreenKey::Backspace);
    check(screen.chooser().path() == before + L"ab", "Backspace takes the last character off");

    h.clipboard_text = L"X";
    screen.key(nla::ScreenKey::Paste);
    check(screen.chooser().path() == before + L"abX", "paste is appended too");

    screen.key(nla::ScreenKey::Tab);
    check(!screen.chooser().editing(), "Tab again puts the field back");
    const std::wstring untouched = screen.chooser().path();
    screen.key(nla::ScreenKey::Backspace);
    check(screen.chooser().path() == untouched,
          "Backspace does nothing while the list has the keys");
}

// The list takes the wheel and the arrow keys, and neither runs off the end.
void scrolling_and_selecting() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    const fs::path root = make_folder();
    screen.choose_path(nla::PickerKind::Image, L"Choose your own disc image", root, {});

    // The picker's offset counts from the top, so the wheel's own sign is the reverse of
    // the Activity panel's: rolling up lowers it.
    screen.wheel(1);
    check(screen.chooser().offset() == 0, "the top of the list is the floor");
    screen.wheel(-2);
    check(screen.chooser().offset() == 2, "rolling down moves the list");

    screen.chooser().set_selected(0);
    screen.key(nla::ScreenKey::Up);
    check(screen.chooser().selected() == 0, "the first row is the top");
    screen.key(nla::ScreenKey::Down);
    check(screen.chooser().selected() == 1, "down moves one row");
    const int last = static_cast<int>(screen.chooser().entries().size()) - 1;
    screen.chooser().set_selected(last);
    screen.key(nla::ScreenKey::Down);
    check(screen.chooser().selected() == last, "the last row is the bottom");
}

// Giving up answers nothing, tells the window to stop waiting for a launch, and closes.
void cancelling() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    const fs::path root = make_folder();
    int answered = 0;
    screen.choose_path(nla::PickerKind::Folder, L"Choose a folder", root,
                       [&](const fs::path&) { ++answered; });

    screen.key(nla::ScreenKey::Escape);
    check(answered == 0, "Escape answers nothing");
    check(h.cancelled == 1, "and gives up a launch that was waiting on the answer");
    check(!screen.capturing(), "and closes the page");
    check(!h.text_input.empty() && !h.text_input.back(), "and the window stops taking text");
}

// The Cancel button is the same as Escape.
void the_cancel_button() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    const fs::path root = make_folder();
    screen.choose_path(nla::PickerKind::Folder, L"Choose a folder", root, {});
    screen.activate(nla::Id::PickerCancel);
    check(h.cancelled == 1, "the cancel button gives the launch up");
    check(!screen.capturing(), "and closes the page");
}

// A page owns its own ids and no others, so the window can ask without knowing the page.
void the_ids_it_owns() {
    RecordingHost h;
    nla::PickerScreen screen(h.host);
    check(screen.handles(nla::Id::PickerUp), "the up row");
    check(screen.handles(nla::Id::PickerPath), "the path field");
    check(screen.handles(nla::Id::PickerAccept), "the accept button");
    check(screen.handles(nla::Id::PickerCancel), "the cancel button");
    check(screen.handles(static_cast<nla::Id>(static_cast<int>(nla::Id::PickerEntry) + 3)),
          "a list row");
    check(!screen.handles(nla::Id::OpenSettings), "and not the Settings button");
    check(!screen.handles(nla::Id::UpdateBanner), "nor the update button");
}

// A system chooser that answers is used instead of the list, and a refusal is reported.
void the_system_chooser() {
    {
        RecordingHost h;
        h.native.shown = true;
        h.native.path = fs::path(L"/tmp/answered.iso");
        nla::PickerScreen screen(h.host);
        fs::path answer;
        screen.choose_path(nla::PickerKind::Image, L"Choose your own disc image", fs::path(L"/"),
                           [&](const fs::path& chosen) { answer = chosen; });
        check(answer == fs::path(L"/tmp/answered.iso"),
              "the system chooser's answer goes straight to the caller");
        check(!screen.capturing(), "and the launcher's own list is never drawn");
    }
    {
        RecordingHost h;
        h.native.refused = true;
        nla::PickerScreen screen(h.host);
        int answered = 0;
        screen.choose_path(nla::PickerKind::Image, L"Choose your own disc image", fs::path(L"/"),
                           [&](const fs::path&) { ++answered; });
        check(screen.capturing(), "a chooser that would not open falls back to the list");
        check(answered == 0, "with nothing answered");
        check(h.logged.size() == 1, "and the fallback is said in the log");
    }
}

int run() {
    opening();
    descending();
    picking_a_file();
    a_missing_folder();
    typing();
    scrolling_and_selecting();
    cancelling();
    the_cancel_button();
    the_ids_it_owns();
    the_system_chooser();
    std::printf("%s\n", failures ? "some checks failed" : "all checks passed");
    return failures ? 1 : 0;
}

}  // namespace

#if defined(_WIN32)
int wmain() { return run(); }
#else
int main() { return run(); }
#endif
