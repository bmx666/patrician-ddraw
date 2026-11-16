#pragma once

#include <windows.h>

namespace sgl {

bool install_mouse_input_transform(HWND window, unsigned output_width, unsigned output_height,
    unsigned render_width, unsigned render_height, unsigned logical_width, unsigned logical_height) noexcept;
void update_mouse_input_transform(unsigned logical_width, unsigned logical_height,
    unsigned render_width, unsigned render_height) noexcept;
void remove_mouse_input_transform() noexcept;

}
