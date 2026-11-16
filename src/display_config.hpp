#pragma once

#include <windows.h>
#include <filesystem>
#include <string>

namespace sgl {

struct DisplaySize { unsigned width, height; };

bool is_supported_display_size(DisplaySize size) noexcept;
bool display_config(HWND window, const std::filesystem::path& path, DisplaySize& size, std::string& error);
bool save_display_config(const std::filesystem::path& path, DisplaySize size, std::string& error);

}
