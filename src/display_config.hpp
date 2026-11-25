#pragma once

#include <windows.h>
#include <filesystem>
#include <optional>
#include <string>

namespace sgl {

struct DisplaySize { unsigned width, height; };

enum class DisplayMode { Windowed, Borderless };

struct DisplayConfig {
    DisplaySize size{};
    DisplayMode mode = DisplayMode::Borderless;
    // [display] max_fps: absent keeps the game's own 50 fps limiter; 0 removes it; 10..1000 sets the limiter's
    // minimum frame time to the nearest millisecond.
    std::optional<unsigned> max_fps;
    // [display] vsync: the default 1 makes presentation wait for the vertical blank, which paces frames evenly and
    // caps them at the refresh rate; 0 presents at once, so max_fps alone limits the frame rate.
    bool vsync = true;
    // [display] show_fps=1 draws frame rate and frame times into the top-left corner.
    bool show_fps = false;
};

bool is_supported_display_size(DisplaySize size) noexcept;
bool display_config(HWND window, const std::filesystem::path& path, DisplayConfig& config, std::string& error);
bool save_display_config(const std::filesystem::path& path, DisplaySize size, DisplayMode mode, std::string& error);

}
