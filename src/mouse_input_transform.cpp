#include "mouse_input_transform.hpp"

#include "screen_transform.hpp"

#include <atomic>

namespace {
HHOOK mouse_message_hook = nullptr;
HWND mouse_window = nullptr;
std::atomic<unsigned> output_width{0};
std::atomic<unsigned> output_height{0};
std::atomic<unsigned> logical_width{0};
std::atomic<unsigned> logical_height{0};
std::atomic<unsigned> render_width{0};
std::atomic<unsigned> render_height{0};

bool has_client_coordinates(UINT message) noexcept {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_MOUSEHOVER:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

bool is_mouse_button_down(UINT message) noexcept {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

bool map_mouse_message(MSG& message) noexcept {
    RECT client{};
    if (!GetClientRect(message.hwnd, &client)) return false;
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return false;

    sgl::ScreenTransform transform;
    if (!sgl::ScreenTransform::create_composed(logical_width.load(std::memory_order_relaxed),
        logical_height.load(std::memory_order_relaxed), render_width.load(std::memory_order_relaxed),
        render_height.load(std::memory_order_relaxed), output_width.load(std::memory_order_relaxed),
        output_height.load(std::memory_order_relaxed), transform)) return false;

    const float client_x = static_cast<short>(LOWORD(message.lParam));
    const float client_y = static_cast<short>(HIWORD(message.lParam));
    float game_x = 0.0f;
    float game_y = 0.0f;
    const bool inside_viewport = transform.inverse_client_point(client_x, client_y,
        static_cast<unsigned>(width), static_cast<unsigned>(height), game_x, game_y);
    if (!inside_viewport && is_mouse_button_down(message.message)) {
        message.message = WM_NULL;
        message.wParam = 0;
        message.lParam = 0;
        return true;
    }

    message.lParam = MAKELPARAM(static_cast<WORD>(static_cast<LONG>(game_x)),
        static_cast<WORD>(static_cast<LONG>(game_y)));
    return true;
}

LRESULT CALLBACK mouse_message_proc(int code, WPARAM remove_mode, LPARAM data) {
    if (code == HC_ACTION && remove_mode == PM_REMOVE && data) {
        auto* message = reinterpret_cast<MSG*>(data);
        if (message->hwnd == mouse_window && has_client_coordinates(message->message))
            map_mouse_message(*message);
    }
    return CallNextHookEx(mouse_message_hook, code, remove_mode, data);
}
}

namespace sgl {

bool install_mouse_input_transform(HWND window, unsigned width, unsigned height,
    unsigned game_render_width, unsigned game_render_height, unsigned game_width,
    unsigned game_height) noexcept {
    if (!window || !IsWindow(window) || !width || !height || !game_render_width ||
        !game_render_height || !game_width || !game_height) return false;
    if (mouse_message_hook) return mouse_window == window;

    DWORD process_id = 0;
    const DWORD thread_id = GetWindowThreadProcessId(window, &process_id);
    if (!thread_id || process_id != GetCurrentProcessId()) return false;

    mouse_window = window;
    output_width.store(width, std::memory_order_relaxed);
    output_height.store(height, std::memory_order_relaxed);
    logical_width.store(game_width, std::memory_order_relaxed);
    logical_height.store(game_height, std::memory_order_relaxed);
    render_width.store(game_render_width, std::memory_order_relaxed);
    render_height.store(game_render_height, std::memory_order_relaxed);
    mouse_message_hook = SetWindowsHookExW(WH_GETMESSAGE, mouse_message_proc, nullptr, thread_id);
    if (mouse_message_hook) return true;

    mouse_window = nullptr;
    output_width.store(0, std::memory_order_relaxed);
    output_height.store(0, std::memory_order_relaxed);
    logical_width.store(0, std::memory_order_relaxed);
    logical_height.store(0, std::memory_order_relaxed);
    render_width.store(0, std::memory_order_relaxed);
    render_height.store(0, std::memory_order_relaxed);
    return false;
}

void update_mouse_input_transform(unsigned width, unsigned height, unsigned game_render_width,
    unsigned game_render_height) noexcept {
    logical_width.store(width, std::memory_order_relaxed);
    logical_height.store(height, std::memory_order_relaxed);
    render_width.store(game_render_width, std::memory_order_relaxed);
    render_height.store(game_render_height, std::memory_order_relaxed);
}

void remove_mouse_input_transform() noexcept {
    if (mouse_message_hook) UnhookWindowsHookEx(mouse_message_hook);
    mouse_message_hook = nullptr;
    mouse_window = nullptr;
    output_width.store(0, std::memory_order_relaxed);
    output_height.store(0, std::memory_order_relaxed);
    logical_width.store(0, std::memory_order_relaxed);
    logical_height.store(0, std::memory_order_relaxed);
    render_width.store(0, std::memory_order_relaxed);
    render_height.store(0, std::memory_order_relaxed);
}

}
