#pragma once

#include <windows.h>
#include <filesystem>
#include <string>

namespace sgl {

struct DisplaySize { unsigned width, height; };

enum class DisplayMode { Windowed, Borderless };

struct DisplayConfig {
    DisplaySize size{};
    DisplayMode mode = DisplayMode::Borderless;
};

bool is_supported_display_size(DisplaySize size) noexcept;
bool display_config(HWND window, const std::filesystem::path& path, DisplayConfig& config, std::string& error);
bool save_display_config(const std::filesystem::path& path, DisplaySize size, DisplayMode mode, std::string& error);

}
