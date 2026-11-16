#include "mouse_input_transform.hpp"

#include <windows.h>

#include <array>

namespace {

bool retrieve(HWND window, UINT message_id, LPARAM coordinates, MSG& message) noexcept {
    if (!PostMessageW(window, message_id, message_id == WM_LBUTTONDOWN ? MK_LBUTTON : 0, coordinates))
        return false;
    return PeekMessageW(&message, window, 0, 0xffff, PM_REMOVE) != FALSE;
}

bool mapped(HWND window, UINT message_id, LPARAM coordinates, int expected_x, int expected_y) noexcept {
    MSG message{};
    return retrieve(window, message_id, coordinates, message) && message.message == message_id &&
        static_cast<short>(LOWORD(message.lParam)) == expected_x &&
        static_cast<short>(HIWORD(message.lParam)) == expected_y;
}

} // namespace

int wmain() {
    const HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 2560, 1440,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 1;

    if (!sgl::install_mouse_input_transform(window, 2560, 1440, 1280, 1024, 800, 600)) {
        DestroyWindow(window);
        return 2;
    }

    MSG message{};
    const bool outside_down_retrieved = retrieve(window, WM_LBUTTONDOWN, MAKELPARAM(100, 720), message);
    const bool outside_down_blocked = outside_down_retrieved && message.message == WM_NULL &&
        message.wParam == 0 && message.lParam == 0;

    const bool outside_move_clamped = mapped(window, WM_MOUSEMOVE, MAKELPARAM(100, 720), 0, 300);
    const bool inside_down_mapped = mapped(window, WM_LBUTTONDOWN, MAKELPARAM(380, 720), 0, 300);
    const bool outside_up_clamped = mapped(window, WM_LBUTTONUP, MAKELPARAM(100, 720), 0, 300);

    struct Mode { unsigned width, height; int left, right, center_x, center_y; };
    constexpr std::array<Mode, 4> modes{{
        {1280, 1024, 380, 2179, 640, 512},
        {1600, 900, 0, 2559, 800, 450},
        {1920, 1080, 0, 2559, 960, 540},
        {2560, 1440, 0, 2559, 1280, 720},
    }};
    bool mode_edges_mapped = true;
    for (const auto& mode : modes) {
        sgl::update_mouse_input_transform(mode.width, mode.height, mode.width, mode.height);
        const bool center_mapped = mapped(window, WM_MOUSEMOVE, MAKELPARAM(1280, 720),
            mode.center_x, mode.center_y);
        const bool left_edge_mapped = mapped(window, WM_LBUTTONDOWN, MAKELPARAM(mode.left, 720),
            0, mode.center_y);
        const bool right_edge_mapped = mapped(window, WM_LBUTTONUP, MAKELPARAM(mode.right, 720),
            static_cast<int>(mode.width) - 1, mode.center_y);
        mode_edges_mapped = center_mapped && left_edge_mapped && right_edge_mapped && mode_edges_mapped;
    }

    sgl::update_mouse_input_transform(1280, 1024, 1280, 1024);
    const bool outside_drag_release_clamped = mapped(window, WM_LBUTTONUP, MAKELPARAM(100, 720), 0, 512);

    sgl::remove_mouse_input_transform();
    DestroyWindow(window);

    return outside_down_blocked && outside_move_clamped && inside_down_mapped && outside_up_clamped &&
        mode_edges_mapped && outside_drag_release_clamped ? 0 : 3;
}
