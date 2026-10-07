// bbport: SDL3 window for the Vulkan swapchain (X11, Wayland or Win32).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <SDL3/SDL.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "sdl_window.h"
#include "bbport_overlay.h"
#include "bbport_settings.h"

namespace Frontend {

namespace {
// bbport: mouse look. The window thread adds up the captured mouse's motion and keeps its
// buttons; the pad sampling (runtime_pad.c) takes them as a right stick and buttons.
std::atomic<float> mouse_dx{0.0f}, mouse_dy{0.0f};
std::atomic<u32> mouse_buttons{0};
std::atomic<bool> mouse_active{false};
void AddMotion(std::atomic<float>& total, float delta) {
    float old = total.load(std::memory_order_relaxed);
    while (!total.compare_exchange_weak(old, old + delta, std::memory_order_relaxed)) {
    }
}
} // namespace

WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title) : width{width_}, height{height_} {
    // Gamepads are sampled by runtime_pad.c; their events are pumped here with the window's.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        UNREACHABLE_MSG("Failed to initialize SDL video: {}", SDL_GetError());
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width_);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height_);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    const char* fullscreen = std::getenv("BB_FULLSCREEN");
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, fullscreen && fullscreen[0] == '1');
    // BB_WINDOW_HIDDEN=1: start-up checks of the presenter without showing a window.
    const char* hidden = std::getenv("BB_WINDOW_HIDDEN");
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, hidden && hidden[0] == '1');
    base_title = title;
    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    ASSERT_MSG(window, "Failed to create window: {}", SDL_GetError());

    const char* driver = SDL_GetCurrentVideoDriver();
    const SDL_PropertiesID wp = SDL_GetWindowProperties(window);
    if (driver && !std::strcmp(driver, "x11")) {
        window_info.type = WindowSystemType::X11;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        window_info.render_surface = reinterpret_cast<void*>(SDL_GetNumberProperty(wp, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    } else if (driver && !std::strcmp(driver, "wayland")) {
        window_info.type = WindowSystemType::Wayland;
        window_info.display_connection = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else if (driver && !std::strcmp(driver, "windows")) {
        // bbport (Windows): a Win32 surface on the window's HWND (vk_platform.cpp).
        window_info.type = WindowSystemType::Windows;
        window_info.render_surface = SDL_GetPointerProperty(wp, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    } else {
        UNREACHABLE_MSG("Unsupported SDL video driver {}", driver ? driver : "(none)");
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    width = w;
    height = h;
    LOG_INFO(Frontend, "Window {}x{} on {}", w, h, driver);
}

WindowSDL::~WindowSDL() {
    SDL_DestroyWindow(window);
}

void WindowSDL::BeginTextInput(const std::string& initial, const std::string& prompt) {
    std::scoped_lock lock{text_mutex};
    text = initial;
    text_prompt = prompt;
    text_state = 0;
    text_requested = true;
}

int WindowSDL::PollTextInput(std::string& out) {
    std::scoped_lock lock{text_mutex};
    out = text;
    return text_state;
}

void WindowSDL::UpdateTextTitle() {
    const std::string title = text_active ? base_title + " \u2014 " + text_prompt + ": " + text + "_  (Enter = OK, Esc = cancel)"
                                          : base_title;
    SDL_SetWindowTitle(window, title.c_str());
    BbOverlay::SetTextPrompt(text_active, text_prompt, text);
}

bool WindowSDL::PollEvents() {
    {
        std::scoped_lock lock{text_mutex};
        if (text_requested) { // SDL text input must be toggled from the window thread
            text_requested = false;
            text_active = true;
            SDL_StartTextInput(window);
            UpdateTextTitle();
        }
    }
    if (!text_active) {
        BbOverlay::UpdateTextInput(window);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            last_mouse_motion_ms = SDL_GetTicks();
        }
        if (text_active && (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_KEY_DOWN)) {
            std::scoped_lock lock{text_mutex};
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                text += event.text.text;
            } else if (event.key.key == SDLK_BACKSPACE && !text.empty()) {
                size_t cut = text.size() - 1; // drop one UTF-8 code point
                while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
                text.erase(cut);
            } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER || event.key.key == SDLK_ESCAPE) {
                text_state = event.key.key == SDLK_ESCAPE ? 2 : 1;
                text_active = false;
                SDL_StopTextInput(window);
            }
            UpdateTextTitle();
            continue;
        }
        if (mouse_captured) {
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                AddMotion(mouse_dx, event.motion.xrel);
                AddMotion(mouse_dy, event.motion.yrel);
                continue;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                const u32 bit = 1u << (event.button.button - 1);
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    mouse_buttons.fetch_or(bit, std::memory_order_relaxed);
                } else {
                    mouse_buttons.fetch_and(~bit, std::memory_order_relaxed);
                }
                continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                mouse_released = true; // free the cursor
                continue;
            }
        } else if (mouse_released && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                   !BbOverlay::CapturesInput()) {
            mouse_released = false; // a click in the window captures it again
            continue;
        }
        if (BbOverlay::HandleEvent(event)) {
            continue;
        }
        switch (event.type) {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            width = w;
            height = h;
            break;
        }
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            is_open = false;
            break;
        default:
            break;
        }
    }
    UpdateMouseCapture();
    UpdateCursor();
    return is_open;
}

// bbport: the mouse is captured (relative mode, cursor hidden) while it can drive the camera:
// mouse look on, no gamepad, the window focused, neither the settings menu nor text entry open.
void WindowSDL::UpdateMouseCapture() {
    const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (!focused) {
        mouse_released = false; // capture again when the window comes back
    }
    const bool want = BbSettings::Get().mouse_look && !SDL_HasGamepad() && focused &&
                      !BbOverlay::CapturesInput() && !text_active && !mouse_released;
    if (want != mouse_captured) {
        mouse_captured = want;
        SDL_SetWindowRelativeMouseMode(window, want);
        mouse_buttons.store(0, std::memory_order_relaxed);
        mouse_dx.store(0.0f, std::memory_order_relaxed);
        mouse_dy.store(0.0f, std::memory_order_relaxed);
        mouse_active.store(want, std::memory_order_relaxed);
    }
}

// Issue #3: the OS cursor over the game. Hidden in fullscreen (the settings menu draws its own),
// and in a window after 3 s without moving the mouse.
void WindowSDL::UpdateCursor() {
    if (mouse_captured) {
        return; // relative mode hides it
    }
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    const bool hide = fullscreen || SDL_GetTicks() - last_mouse_motion_ms > 3000;
    if (hide != cursor_hidden) {
        cursor_hidden = hide;
        hide ? SDL_HideCursor() : SDL_ShowCursor();
    }
}

} // namespace Frontend

// bbport: mouse look for the pad sampling. Returns 0 when the mouse is not captured. The right
// stick follows the mouse's speed, measured over at least 8 ms (the game may read the pad more
// than once a frame); a small offset carries slow movements past the game's dead zone.
extern "C" int bbgpu_mouse_look(int* right_x, int* right_y, unsigned* buttons) {
    using namespace Frontend;
    if (!mouse_active.load(std::memory_order_relaxed)) {
        return 0;
    }
    static u64 last_ns = 0;
    static int stick_x = 128, stick_y = 128;
    const u64 now = SDL_GetTicksNS();
    if (now - last_ns >= 8'000'000ull) {
        const float seconds = last_ns ? float(now - last_ns) * 1e-9f : 0.016f;
        last_ns = now;
        const float dx = mouse_dx.exchange(0.0f, std::memory_order_relaxed);
        const float dy = mouse_dy.exchange(0.0f, std::memory_order_relaxed);
        const float scale = 0.085f * BbSettings::Get().mouse_sensitivity.load();
        const auto deflect = [&](float delta) {
            const float v = delta / std::min(seconds, 0.1f) * scale; // pixels/s to stick units
            if (std::fabs(v) < 0.5f) {
                return 128;
            }
            const float out = std::copysign(std::min(24.0f + std::fabs(v), 127.0f), v);
            return int(128.0f + out);
        };
        stick_x = std::clamp(deflect(dx), 0, 255);
        stick_y = std::clamp(deflect(dy), 0, 255);
    }
    *right_x = stick_x;
    *right_y = stick_y;
    *buttons = mouse_buttons.load(std::memory_order_relaxed);
    return 1;
}
