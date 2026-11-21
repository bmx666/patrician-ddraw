#pragma once

#include <windows.h>

namespace sgl {

// Windowed mode only. The original executable treats its window as a fullscreen surface: on every mode change it
// moves it to (0,0) as HWND_TOPMOST, it captures the mouse permanently on the first move into the client area and it
// warps the cursor with game (logical) coordinates. These bindings redirect the executable's USER32 imports so the
// window stays where the user placed it, capture is left to the presentation (held only while a button is down) and
// cursor warps land on the matching physical client pixel. Calls for other windows pass through unchanged.
bool install_game_window_binding(HWND window) noexcept;
void remove_game_window_binding() noexcept;

}
