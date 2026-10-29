#include "window.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#if defined(_WIN32)
#include <SDL_syswm.h>
#include <dwmapi.h>
#endif

namespace nla {
namespace {

wstring sdl_error() { return widen(SDL_GetError()); }

// How many physical pixels one logical unit covers on the given display.
float display_scale(int index) {
#if defined(_WIN32)
    // Windows keeps window coordinates in pixels, so ask the monitor for its DPI.
    float dpi = 0.0f;
    if (index >= 0 && SDL_GetDisplayDPI(index, nullptr, &dpi, nullptr) == 0 && dpi > 48.0f)
        return dpi / 96.0f;
    return 1.0f;
#else
    // Elsewhere the window is sized in points and drawn in pixels, so the ratio is the
    // scale: 2 on a Retina display.
    (void)index;
    return 1.0f;
#endif
}

}  // namespace

Window::~Window() { destroy(); }

bool Window::create(const wstring& title, int logical_width, int logical_height,
                    wstring* error) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        if (error) *error = L"SDL could not start: " + sdl_error();
        return false;
    }
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");

    int window_width = logical_width;
    int window_height = logical_height;
#if defined(_WIN32)
    // On Windows SDL's window coordinates are physical pixels, so a 1060-point layout
    // needs a window 1.5× that wide on a 150% display.
    float content = display_scale(0);
    window_width = static_cast<int>(std::lround(logical_width * content));
    window_height = static_cast<int>(std::lround(logical_height * content));
#endif

    window_ = SDL_CreateWindow(narrow(title).c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, window_width, window_height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        if (error) *error = L"cannot create the window: " + sdl_error();
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        if (error) *error = L"cannot create a renderer: " + sdl_error();
        return false;
    }
    SDL_SetRenderDrawColor(renderer_, 0x12, 0x14, 0x1A, 0xFF);

    hand_cursor_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    arrow_cursor_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    apply_dark_title_bar();
    return true;
}

void Window::destroy() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (hand_cursor_) SDL_FreeCursor(hand_cursor_);
    if (arrow_cursor_) SDL_FreeCursor(arrow_cursor_);
    if (window_) SDL_DestroyWindow(window_);
    if (window_ || renderer_) SDL_Quit();
    texture_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
    hand_cursor_ = nullptr;
    arrow_cursor_ = nullptr;
}

void Window::show() { SDL_ShowWindow(window_); }

void Window::set_title(const wstring& title) {
    if (window_) SDL_SetWindowTitle(window_, narrow(title).c_str());
}

void Window::set_min_size(int logical_width, int logical_height) {
    SDL_SetWindowMinimumSize(window_, logical_width, logical_height);
}

float Window::scale() const {
    if (!window_) return 1.0f;
#if defined(_WIN32)
    return display_scale(SDL_GetWindowDisplayIndex(window_));
#else
    int points_w = 0, points_h = 0, pixels_w = 0, pixels_h = 0;
    SDL_GetWindowSize(window_, &points_w, &points_h);
    SDL_GetWindowSizeInPixels(window_, &pixels_w, &pixels_h);
    if (points_w <= 0) return 1.0f;
    float factor = static_cast<float>(pixels_w) / static_cast<float>(points_w);
    return factor > 0.1f ? factor : 1.0f;
#endif
}

void Window::set_ui_zoom(float zoom) { zoom_ = zoom > 0.05f ? zoom : 1.0f; }

float Window::pointer_scale() const {
#if defined(__APPLE__)
    // macOS reports pointer positions in points, which the layout divides by the zoom.
    return zoom_;
#else
    return scale() * zoom_;
#endif
}

void Window::pixel_size(int* width, int* height) const {
    int pixels_w = 0, pixels_h = 0;
    if (window_) SDL_GetWindowSizeInPixels(window_, &pixels_w, &pixels_h);
    if (width) *width = std::max(1, pixels_w);
    if (height) *height = std::max(1, pixels_h);
}

bool Window::poll(SDL_Event& event) const { return SDL_PollEvent(&event) != 0; }

bool Window::wait(SDL_Event& event, int timeout_ms) const {
    return SDL_WaitEventTimeout(&event, timeout_ms) != 0;
}

void Window::present(const std::uint32_t* pixels, int width, int height) {
    if (!renderer_ || !window_ || !pixels) return;
    if (!texture_ || texture_width_ != width || texture_height_ != height) {
        if (texture_) SDL_DestroyTexture(texture_);
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING, width, height);
        texture_width_ = width;
        texture_height_ = height;
    }
    if (!texture_) return;
    SDL_UpdateTexture(texture_, nullptr, pixels, width * static_cast<int>(sizeof(std::uint32_t)));
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
    invalidated_ = false;
}

bool Window::take_invalidated() {
    bool value = invalidated_;
    invalidated_ = false;
    return value;
}

void Window::set_hand_cursor(bool hand) {
    if (hand == hand_shown_) return;
    hand_shown_ = hand;
    SDL_SetCursor(hand ? hand_cursor_ : arrow_cursor_);
}

void Window::set_text_input(bool enabled) {
    if (enabled) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

void Window::set_clipboard(const wstring& text) {
    SDL_SetClipboardText(narrow(text).c_str());
}

wstring Window::clipboard() const {
    char* text = SDL_GetClipboardText();
    wstring value;
    if (text) {
        value = widen(text);
        SDL_free(text);
    }
    return value;
}

void Window::apply_dark_title_bar() {
#if defined(_WIN32)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window_, &info)) return;
    BOOL dark = TRUE;
    // 20 is DWMWA_USE_IMMERSIVE_DARK_MODE before the Windows 11 SDK named it.
    DwmSetWindowAttribute(info.info.win.window, 20, &dark, sizeof(dark));
#endif
}

void Dispatcher::initialise() {
    type_ = SDL_RegisterEvents(1);
    if (type_ == static_cast<unsigned>(-1)) type_ = 0;
}

void Dispatcher::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(std::move(task));
    }
    if (!type_) return;
    SDL_Event event{};
    event.type = type_;
    SDL_PushEvent(&event);
}

void Dispatcher::drain() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(pending_);
    }
    for (auto& task : tasks) task();
}

}  // namespace nla
