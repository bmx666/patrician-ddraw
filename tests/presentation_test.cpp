#include "presentation.hpp"
#include "screen_transform.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <commctrl.h>

using Microsoft::WRL::ComPtr;

namespace {

struct FixedWindowSize {
    LONG width;
    LONG height;
};

LRESULT CALLBACK fixed_window_size_subclass(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference_data) {
    if (message == WM_WINDOWPOSCHANGING && lparam && reference_data) {
        const auto* size = reinterpret_cast<const FixedWindowSize*>(reference_data);
        auto* position = reinterpret_cast<WINDOWPOS*>(lparam);
        if (!(position->flags & SWP_NOSIZE)) {
            position->cx = size->width;
            position->cy = size->height;
        }
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

bool windowed_size_fits_monitor(HWND window, UINT width, UINT height) {
    const LONG_PTR style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME) & ~WS_MAXIMIZEBOX;
    const LONG_PTR extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (!AdjustWindowRectEx(&rect, static_cast<DWORD>(style), GetMenu(window) != nullptr,
        static_cast<DWORD>(extended_style))) return false;
    const HMONITOR handle = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!handle || !GetMonitorInfoW(handle, &info)) return false;
    return rect.right - rect.left <= info.rcWork.right - info.rcWork.left &&
        rect.bottom - rect.top <= info.rcWork.bottom - info.rcWork.top;
}

bool simulate_game_style_reset(HWND window) {
    constexpr LONG_PTR game_style = WS_VISIBLE | WS_CLIPSIBLINGS | WS_SYSMENU | WS_MINIMIZEBOX;
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, GWL_STYLE, game_style);
    if (!previous && GetLastError() != ERROR_SUCCESS) return false;
    return SetWindowPos(window, nullptr, 0, 0, 640, 480,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED) != FALSE;
}

bool has_fixed_windowed_style(HWND window) {
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    return !(style & (WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX)) && (style & WS_CAPTION);
}

}

int main() {
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 8, 8,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 1;
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return 13;
    const LONG monitor_width = monitor.rcMonitor.right - monitor.rcMonitor.left;
    const LONG monitor_height = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context))) return 2;
        sgl::Presentation presentation;
        if (presentation.present() != E_UNEXPECTED || presentation.initialize(device.Get(), window, 0, 8) != E_INVALIDARG ||
            FAILED(presentation.initialize(device.Get(), window, 8, 8))) return 3;
        RECT client = {};
        if (!GetClientRect(window, &client) || client.right - client.left != monitor_width ||
            client.bottom - client.top != monitor_height || !(GetWindowLongPtrW(window, GWL_STYLE) & WS_POPUP)) return 14;
        const auto render_size = presentation.render_size();
        const auto output_size = presentation.output_size();
        if (render_size.width != 8 || render_size.height != 8 ||
            output_size.width != static_cast<UINT>(monitor_width) ||
            output_size.height != static_cast<UINT>(monitor_height)) return 15;
        D3D11_TEXTURE2D_DESC back_buffer_desc = {};
        presentation.back_buffer()->GetDesc(&back_buffer_desc);
        if (back_buffer_desc.Width != static_cast<UINT>(monitor_width) ||
            back_buffer_desc.Height != static_cast<UINT>(monitor_height)) return 16;
        for (const auto& requested : std::array<std::array<int, 2>, 2>{{{1280, 1024}, {800, 600}}}) {
            if (!SetWindowPos(window, nullptr, 0, 0, requested[0], requested[1],
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW)) return 17;
            RECT guarded_client = {};
            RECT guarded_window = {};
            if (!GetClientRect(window, &guarded_client) || !GetWindowRect(window, &guarded_window) ||
                guarded_client.right - guarded_client.left != monitor_width ||
                guarded_client.bottom - guarded_client.top != monitor_height ||
                guarded_window.left != monitor.rcMonitor.left || guarded_window.top != monitor.rcMonitor.top ||
                guarded_window.right != monitor.rcMonitor.right || guarded_window.bottom != monitor.rcMonitor.bottom) return 18;
        }
        sgl::Compositor compositor;
        if (FAILED(compositor.initialize(device.Get()))) return 4;
        const std::array<std::uint32_t, 4> pixels = {0xff112233, 0xff445566, 0xff778899, 0xffaabbcc};
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = 2;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA data = {pixels.data(), 8, 0};
        ComPtr<ID3D11Texture2D> source_texture;
        ComPtr<ID3D11ShaderResourceView> source;
        if (FAILED(device->CreateTexture2D(&desc, &data, &source_texture)) ||
            FAILED(device->CreateShaderResourceView(source_texture.Get(), nullptr, &source))) return 5;
        if (FAILED(presentation.draw(context.Get(), compositor, source.Get(), {0, 0, 2, 2}, {0, 0, 8, 8}))) return 6;
        presentation.back_buffer()->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return 7;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(staging.Get(), presentation.back_buffer());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 8;
        const auto sample = [&](UINT x, UINT y) {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
            std::uint32_t value = 0;
            std::memcpy(&value, row + x * 4, sizeof(value));
            return value;
        };
        sgl::ScreenTransform transform;
        if (!sgl::ScreenTransform::create(8, 8, static_cast<unsigned>(monitor_width),
            static_cast<unsigned>(monitor_height), transform)) return 20;
        const UINT left = static_cast<UINT>(transform.offset_x + transform.scale * 2.0);
        const UINT right = static_cast<UINT>(transform.offset_x + transform.scale * 6.0);
        const UINT top = static_cast<UINT>(transform.offset_y + transform.scale * 2.0);
        const UINT bottom = static_cast<UINT>(transform.offset_y + transform.scale * 6.0);
        const bool matches = sample(left, top) == pixels[0] && sample(right, top) == pixels[1] &&
            sample(left, bottom) == pixels[2] && sample(right, bottom) == pixels[3] &&
            (transform.offset_x == 0.0 || sample(0, static_cast<UINT>(monitor_height / 2)) == 0xff000000) &&
            (transform.offset_y == 0.0 || sample(static_cast<UINT>(monitor_width / 2), 0) == 0xff000000);
        context->Unmap(staging.Get(), 0);
        if (!matches) return 9;
        if (FAILED(presentation.present())) return 10;
        presentation.close();
        RECT released_client = {};
        if (!SetWindowPos(window, nullptr, 0, 0, 800, 600, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW) ||
            !GetClientRect(window, &released_client) || released_client.right - released_client.left != 800 ||
            released_client.bottom - released_client.top != 600) return 19;
        for (const auto& size : std::array<std::array<UINT, 2>, 4>{{
            {1280, 1024}, {1600, 900}, {1920, 1080}, {2560, 1440}}}) {
            sgl::Presentation configured;
            if (FAILED(configured.initialize(device.Get(), window, size[0], size[1]))) return 11;
            const auto selected = configured.render_size();
            const auto output = configured.output_size();
            D3D11_TEXTURE2D_DESC actual = {};
            configured.back_buffer()->GetDesc(&actual);
            if (selected.width != size[0] || selected.height != size[1] ||
                output.width != static_cast<UINT>(monitor_width) || output.height != static_cast<UINT>(monitor_height) ||
                actual.Width != output.width || actual.Height != output.height) return 12;
        }
        constexpr std::array<std::array<UINT, 2>, 4> resolutions = {{
            {1280, 1024}, {1600, 900}, {1920, 1080}, {2560, 1440}
        }};
        const LONG_PTR borderless_style = GetWindowLongPtrW(window, GWL_STYLE);
        for (std::size_t i = 0; i < resolutions.size(); ++i) {
            const auto& size = resolutions[i];
            sgl::Presentation configured;
            const HRESULT initialized = configured.initialize(device.Get(), window, size[0], size[1], sgl::DisplayMode::Windowed);
            if (!windowed_size_fits_monitor(window, size[0], size[1])) {
                RECT unchanged = {};
                if (initialized != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) ||
                    GetWindowLongPtrW(window, GWL_STYLE) != borderless_style || !GetWindowRect(window, &unchanged) ||
                    unchanged.left != monitor.rcMonitor.left || unchanged.top != monitor.rcMonitor.top ||
                    unchanged.right != monitor.rcMonitor.right || unchanged.bottom != monitor.rcMonitor.bottom) return 21;
                continue;
            }
            if (FAILED(initialized) || configured.display_mode() != sgl::DisplayMode::Windowed) return 22;
            const auto output = configured.output_size();
            RECT windowed_client = {};
            D3D11_TEXTURE2D_DESC actual = {};
            configured.back_buffer()->GetDesc(&actual);
            const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
            if (!GetClientRect(window, &windowed_client) || windowed_client.right - windowed_client.left != static_cast<LONG>(size[0]) ||
                windowed_client.bottom - windowed_client.top != static_cast<LONG>(size[1]) || output.width != size[0] ||
                output.height != size[1] || actual.Width != size[0] || actual.Height != size[1] ||
                (style & WS_POPUP) || !(style & WS_CAPTION) || (style & WS_THICKFRAME) || (style & WS_MAXIMIZEBOX)) return 23;

            if (!simulate_game_style_reset(window) || !GetClientRect(window, &windowed_client) ||
                windowed_client.right - windowed_client.left != static_cast<LONG>(size[0]) ||
                windowed_client.bottom - windowed_client.top != static_cast<LONG>(size[1]) ||
                !has_fixed_windowed_style(window) || configured.set_render_size(size[0], size[1]) != S_OK ||
                !GetClientRect(window, &windowed_client) ||
                windowed_client.right - windowed_client.left != static_cast<LONG>(size[0]) ||
                windowed_client.bottom - windowed_client.top != static_cast<LONG>(size[1]) ||
                !has_fixed_windowed_style(window)) return 32;

            const auto& next = resolutions[(i + 1) % resolutions.size()];
            if (configured.set_render_size(800, 600) != E_INVALIDARG) return 24;
            if (!windowed_size_fits_monitor(window, next[0], next[1])) {
                if (!simulate_game_style_reset(window)) return 33;
                if (configured.set_render_size(next[0], next[1]) != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) ||
                    configured.output_size().width != size[0] || configured.output_size().height != size[1] ||
                    configured.render_size().width != size[0] || configured.render_size().height != size[1] ||
                    !GetClientRect(window, &windowed_client) ||
                    windowed_client.right - windowed_client.left != static_cast<LONG>(size[0]) ||
                    windowed_client.bottom - windowed_client.top != static_cast<LONG>(size[1]) ||
                    !has_fixed_windowed_style(window)) return 25;
            } else if (!simulate_game_style_reset(window) || configured.set_render_size(next[0], next[1]) != S_OK) {
                return 26;
            }
            const auto resized_output = configured.output_size();
            if (windowed_size_fits_monitor(window, next[0], next[1]) &&
                (resized_output.width != next[0] || resized_output.height != next[1] ||
                    configured.render_size().width != next[0] || configured.render_size().height != next[1] ||
                    !GetClientRect(window, &windowed_client) || windowed_client.right - windowed_client.left != static_cast<LONG>(next[0]) ||
                    windowed_client.bottom - windowed_client.top != static_cast<LONG>(next[1]) ||
                    !has_fixed_windowed_style(window))) return 27;
            if (windowed_size_fits_monitor(window, next[0], next[1])) {
                RECT before_failure = {};
                if (!GetWindowRect(window, &before_failure)) return 29;
                const FixedWindowSize fixed_size{before_failure.right - before_failure.left,
                    before_failure.bottom - before_failure.top};
                constexpr UINT_PTR subclass_id = 1;
                if (!SetWindowSubclass(window, fixed_window_size_subclass, subclass_id,
                    reinterpret_cast<DWORD_PTR>(&fixed_size))) return 30;
                const HRESULT rejected = configured.set_render_size(size[0], size[1]);
                const BOOL removed = RemoveWindowSubclass(window, fixed_window_size_subclass, subclass_id);
                RECT after_failure = {};
                const auto unchanged_output = configured.output_size();
                const auto unchanged_render = configured.render_size();
                if (!removed || rejected != E_FAIL || !GetWindowRect(window, &after_failure) ||
                    std::memcmp(&before_failure, &after_failure, sizeof(RECT)) != 0 ||
                    unchanged_output.width != next[0] || unchanged_output.height != next[1] ||
                    unchanged_render.width != next[0] || unchanged_render.height != next[1]) return 31;
            }
            configured.close();
            if (GetWindowLongPtrW(window, GWL_STYLE) != borderless_style) return 28;
        }
    }
    DestroyWindow(window);
    return 0;
}
