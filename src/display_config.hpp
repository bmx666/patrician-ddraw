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
    // [display] max_fps: absent keeps the game's own 50 fps limiter; 0 removes it (presentation still waits for
    // vertical blank); 10..1000 sets the limiter's minimum frame time to the nearest millisecond.
    std::optional<unsigned> max_fps;
};

bool is_supported_display_size(DisplaySize size) noexcept;
bool display_config(HWND window, const std::filesystem::path& path, DisplayConfig& config, std::string& error);
bool save_display_config(const std::filesystem::path& path, DisplaySize size, DisplayMode mode, std::string& error);

}
