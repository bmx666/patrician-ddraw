#pragma once

#include <windows.h>

namespace sgl {

bool install_mouse_input_transform(HWND window, unsigned render_width, unsigned render_height,
    unsigned logical_width, unsigned logical_height) noexcept;
void update_mouse_input_transform(unsigned logical_width, unsigned logical_height,
    unsigned render_width, unsigned render_height) noexcept;
void remove_mouse_input_transform() noexcept;
// Maps a game (logical) client point to the physical client pixel at its center.
bool map_game_point_to_client(int& x, int& y) noexcept;

}
