#include "presentation.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main() {
    wchar_t directory[MAX_PATH] = {}, filename[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"p3d", 0, filename)) return 1;
    const std::filesystem::path path(filename);
    struct RemoveFile { std::filesystem::path path; ~RemoveFile() { std::error_code error; std::filesystem::remove(path, error); } } remove_file{path};
    const auto write = [&path](const char* text) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        return static_cast<bool>(output);
    };
    const auto read = [&path]() {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    sgl::DisplaySize size{};
    std::string error;
    constexpr std::array<std::array<unsigned, 2>, 4> supported = {{
        {{1280, 1024}}, {{1600, 900}}, {{1920, 1080}}, {{2560, 1440}}
    }};
    for (const auto& resolution : supported) {
        const auto configuration = "[display]\nwidth=" + std::to_string(resolution[0]) +
            "\nheight=" + std::to_string(resolution[1]) + "\n";
        if (!write(configuration.c_str()) || !sgl::display_config(GetDesktopWindow(), path, size, error) ||
            size.width != resolution[0] || size.height != resolution[1]) return 2;
    }
    for (const char* invalid : {"[display]\nwidth=800\nheight=600\n", "[display]\nwidth=1024\nheight=768\n",
        "[display]\nwidth=1920\n", "[display]\nwidth=1920\nheight=1081\n",
        "[display]\nwidth=1920\nwidth=1920\nheight=1080\n", "[display]\nwidth=1920x\nheight=1080\n",
        "[display]\nwidth=1920\nheight=1080\nextra=1\n", "[display]\nwidth 1920\nheight=1080\n",
        "[display]\nwidth=\nheight=1080\n"}) {
        if (!write(invalid) || sgl::display_config(GetDesktopWindow(), path, size, error) || error.empty()) return 3;
    }
    if (!write("; keep this comment\n[other]\nvalue=preserved\n[display]\nheight=1080\n[tail]\nvalue=also preserved\n") ||
        !sgl::save_display_config(path, {2560, 1440}, error)) return 13;
    const auto saved = read();
    if (saved.find("; keep this comment\r\n[other]\r\nvalue=preserved\r\n[display]\r\nheight=1440\r\nwidth=2560\r\n[tail]\r\nvalue=also preserved\r\n") == std::string::npos ||
        !sgl::display_config(GetDesktopWindow(), path, size, error) || size.width != 2560 || size.height != 1440) return 14;
    const HANDLE keep_destination_open = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (keep_destination_open == INVALID_HANDLE_VALUE) return 19;
    const bool replaced_open_file = sgl::save_display_config(path, {1920, 1080}, error);
    const auto unchanged = read();
    CloseHandle(keep_destination_open);
    if (replaced_open_file || error.empty() || unchanged != saved) return 20;
    if (sgl::save_display_config(path, {1920, 1200}, error) || error.empty() || read() != saved) return 15;
    if (sgl::save_display_config(path, {1024, 768}, error) || error.empty() || read() != saved) return 17;
    if (sgl::save_display_config(path, {800, 600}, error) || error.empty() || read() != saved) return 18;
    auto missing_parent = path;
    missing_parent += L".missing-parent\\high_res.ini";
    if (sgl::save_display_config(missing_parent, {1920, 1080}, error) || error.empty()) return 16;
    if (!write("[display]\nwidth=1920\nheight=1080\n")) return 4;
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 8, 8,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 5;
    MONITORINFO output_monitor = {};
    output_monitor.cbSize = sizeof(output_monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &output_monitor)) return 6;
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, nullptr))) return 7;
        sgl::Presentation presentation;
        if (FAILED(presentation.initialize_configured(device.Get(), window, path, error))) return 8;
        const auto render_size = presentation.render_size();
        const auto output_size = presentation.output_size();
        D3D11_TEXTURE2D_DESC desc = {};
        presentation.back_buffer()->GetDesc(&desc);
        if (render_size.width != 1920 || render_size.height != 1080 ||
            output_size.width != static_cast<UINT>(output_monitor.rcMonitor.right - output_monitor.rcMonitor.left) ||
            output_size.height != static_cast<UINT>(output_monitor.rcMonitor.bottom - output_monitor.rcMonitor.top) ||
            desc.Width != output_size.width || desc.Height != output_size.height) return 9;
    }
    DestroyWindow(window);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) return 9;
    if (sgl::display_config(nullptr, path, size, error) || error.empty()) return 12;
    MONITORINFOEXW monitor = {};
    monitor.cbSize = sizeof(monitor);
    const HMONITOR handle = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    if (!handle || !GetMonitorInfoW(handle, &monitor) || !EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) return 10;
    const bool native_supported = (mode.dmPelsWidth == 1280 && mode.dmPelsHeight == 1024) ||
        (mode.dmPelsWidth == 1600 && mode.dmPelsHeight == 900) ||
        (mode.dmPelsWidth == 1920 && mode.dmPelsHeight == 1080) || (mode.dmPelsWidth == 2560 && mode.dmPelsHeight == 1440);
    const bool selected = sgl::display_config(GetDesktopWindow(), path, size, error);
    if (selected != native_supported || (selected && (size.width != mode.dmPelsWidth || size.height != mode.dmPelsHeight)) ||
        (!selected && error.empty())) return 11;
    return 0;
}
