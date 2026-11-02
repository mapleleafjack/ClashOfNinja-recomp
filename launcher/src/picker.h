#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "ui.h"

namespace nla {

namespace fs = std::filesystem;

// What the picker is choosing. It decides what the list offers, and what an answer must be.
enum class PickerKind { None, Folder, Image, Patch };

// One row of the picker's list: a folder to descend into, or a file that can be chosen.
struct PickerEntry {
    std::wstring name;
    bool directory = false;
};

// The launcher's own chooser, for the systems whose own is missing or will not open, and for
// a render, which has no window for either.
//
// It keeps where it is looking and what is listed there. The ordering is part of it rather
// than of the drawing: the parent folder first, then the folders, then the files the kind
// offers, each sorted by name with the case ignored, and dotfiles left out because they are
// not what anyone is looking for.
class Picker {
  public:
    // Opens on `start`, or on the folder it names; nothing is listed until list().
    void open(PickerKind kind, std::wstring title, const fs::path& start,
              std::function<void(const fs::path&)> accept);
    void close();

    bool is_open() const { return kind_ != PickerKind::None; }
    PickerKind kind() const { return kind_; }
    const std::wstring& title() const { return title_; }
    std::wstring& title() { return title_; }
    const std::wstring& folder() const { return folder_; }
    std::wstring& folder() { return folder_; }
    const std::wstring& path() const { return path_; }
    std::wstring& path() { return path_; }
    const std::wstring& note() const { return note_; }
    std::wstring& note() { return note_; }
    bool editing() const { return editing_; }
    void set_editing(bool on) { editing_ = on; }
    int selected() const { return selected_; }
    void set_selected(int index) { selected_ = index; }
    int offset() const { return offset_; }
    void set_offset(int offset) { offset_ = offset; }
    const std::vector<PickerEntry>& entries() const { return entries_; }
    // The work the answer owes, taken out so closing the picker does not drop it.
    std::function<void(const fs::path&)> take_accept() { return std::move(accept_); }

    // Lists the folder it is in.
    void list();

  private:
    PickerKind kind_ = PickerKind::None;
    std::wstring title_;
    std::wstring folder_;
    std::wstring path_;
    std::wstring note_;
    bool editing_ = false;
    int selected_ = -1;
    int offset_ = 0;
    std::vector<PickerEntry> entries_;
    std::function<void(const fs::path&)> accept_;
};

}  // namespace nla
