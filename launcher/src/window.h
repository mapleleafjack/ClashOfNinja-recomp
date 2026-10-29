#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <SDL.h>

#include "util.h"

namespace nla {

// The operating-system window: it owns the SDL window and blits the painter's buffer.
class Window {
  public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const wstring& title, int logical_width, int logical_height, wstring* error);
    void destroy();
    void show();
    void set_title(const wstring& title);
    void set_min_size(int logical_width, int logical_height);

    // Pixels per logical unit, from the display the window is on.
    float scale() const;
    // How much the interface is zoomed up for a large window, on top of the display's own
    // scale. Set from the size the window was resized to.
    void set_ui_zoom(float zoom);
    float ui_zoom() const { return zoom_; }
    // The factor that turns a pointer position into logical units. SDL reports pointer
    // positions in window coordinates, which are points on macOS and pixels elsewhere.
    float pointer_scale() const;
    void pixel_size(int* width, int* height) const;

    bool poll(SDL_Event& event) const;
    bool wait(SDL_Event& event, int timeout_ms) const;

    void present(const std::uint32_t* pixels, int width, int height);
    void invalidate() { invalidated_ = true; }
    bool take_invalidated();

    void set_hand_cursor(bool hand);
    void set_text_input(bool enabled);

    void set_clipboard(const wstring& text);
    wstring clipboard() const;

    SDL_Window* handle() const { return window_; }

  private:
    void apply_dark_title_bar();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_Cursor* hand_cursor_ = nullptr;
    SDL_Cursor* arrow_cursor_ = nullptr;
    bool hand_shown_ = false;
    bool invalidated_ = true;
    int texture_width_ = 0;
    int texture_height_ = 0;
    float zoom_ = 1.0f;
};

// Hands work from the job and game threads to the UI thread through the event queue.
class Dispatcher {
  public:
    void initialise();
    void post(std::function<void()> task);
    void drain();
    unsigned event_type() const { return type_; }

  private:
    std::mutex mutex_;
    std::vector<std::function<void()>> pending_;
    unsigned type_ = 0;
};

}  // namespace nla
