#include "presentation.hpp"
#include "screen_transform.hpp"

#include <commctrl.h>

using Microsoft::WRL::ComPtr;

namespace {

HRESULT size_window_to_monitor(HWND window, RECT& output_rect) noexcept {
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (!monitor) return E_FAIL;
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    RECT previous_rect = {};
    if (!GetMonitorInfoW(monitor, &info)) return HRESULT_FROM_WIN32(GetLastError());
    if (!GetWindowRect(window, &previous_rect)) return HRESULT_FROM_WIN32(GetLastError());

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous_style = GetWindowLongPtrW(window, GWL_STYLE);
    DWORD error = GetLastError();
    if (!previous_style && error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    if (previous_style & WS_CHILD) return E_INVALIDARG;

    const LONG_PTR borderless_style = (previous_style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) | WS_POPUP;
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR replaced_style = SetWindowLongPtrW(window, GWL_STYLE, borderless_style);
    error = GetLastError();
    if (!replaced_style && error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);

    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;
    if (!SetWindowPos(window, nullptr, info.rcMonitor.left, info.rcMonitor.top, width, height,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
        error = GetLastError();
        SetWindowLongPtrW(window, GWL_STYLE, previous_style);
        SetWindowPos(window, nullptr, previous_rect.left, previous_rect.top,
            previous_rect.right - previous_rect.left, previous_rect.bottom - previous_rect.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        return HRESULT_FROM_WIN32(error);
    }

    RECT client = {};
    SetLastError(ERROR_SUCCESS);
    const BOOL got_client = GetClientRect(window, &client);
    error = GetLastError();
    if (!got_client || client.right - client.left != width || client.bottom - client.top != height) {
        SetWindowLongPtrW(window, GWL_STYLE, previous_style);
        SetWindowPos(window, nullptr, previous_rect.left, previous_rect.top,
            previous_rect.right - previous_rect.left, previous_rect.bottom - previous_rect.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        return error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(error);
    }
    output_rect = info.rcMonitor;
    return S_OK;
}

HRESULT get_window_value(HWND window, int index, LONG_PTR& value) noexcept {
    SetLastError(ERROR_SUCCESS);
    value = GetWindowLongPtrW(window, index);
    const DWORD error = GetLastError();
    return value || error == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(error);
}

void restore_window(HWND window, LONG_PTR style, LONG_PTR extended_style, const RECT& rect) noexcept {
    SetWindowLongPtrW(window, GWL_STYLE, style);
    SetWindowLongPtrW(window, GWL_EXSTYLE, extended_style);
    SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}

HRESULT place_windowed_client(HWND window, UINT width, UINT height, RECT* previous_rect = nullptr) noexcept {
    if (width > LONG_MAX || height > LONG_MAX) return E_INVALIDARG;
    LONG_PTR original_style = 0;
    LONG_PTR extended_style = 0;
    HRESULT hr = get_window_value(window, GWL_STYLE, original_style);
    if (FAILED(hr)) return hr;
    hr = get_window_value(window, GWL_EXSTYLE, extended_style);
    if (FAILED(hr)) return hr;
    if (original_style & WS_CHILD) return E_INVALIDARG;
    RECT prior = {};
    if (!GetWindowRect(window, &prior)) return HRESULT_FROM_WIN32(GetLastError());

    constexpr LONG_PTR retained = WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    constexpr LONG_PTR windowed = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME) & ~WS_MAXIMIZEBOX;
    const LONG_PTR style = (original_style & retained) | windowed;

    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (!AdjustWindowRectEx(&rect, static_cast<DWORD>(style), GetMenu(window) != nullptr,
        static_cast<DWORD>(extended_style))) {
        const DWORD error = GetLastError();
        return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!monitor) return E_FAIL;
    if (!GetMonitorInfoW(monitor, &info)) return HRESULT_FROM_WIN32(GetLastError());
    const LONG window_width = rect.right - rect.left;
    const LONG window_height = rect.bottom - rect.top;
    const LONG work_width = info.rcWork.right - info.rcWork.left;
    const LONG work_height = info.rcWork.bottom - info.rcWork.top;
    if (window_width > work_width || window_height > work_height)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    const LONG left = info.rcWork.left + (work_width > window_width ? (work_width - window_width) / 2 : 0);
    const LONG top = info.rcWork.top + (work_height > window_height ? (work_height - window_height) / 2 : 0);

    const auto rollback = [&]() noexcept {
        restore_window(window, original_style, extended_style, prior);
    };

    if (style != original_style) {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR replaced = SetWindowLongPtrW(window, GWL_STYLE, style);
        const DWORD error = GetLastError();
        if (!replaced && error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    }
    if (!SetWindowPos(window, nullptr, left, top, window_width, window_height,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
        const DWORD error = GetLastError();
        rollback();
        return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }

    RECT client = {};
    if (!GetClientRect(window, &client)) {
        const DWORD error = GetLastError();
        rollback();
        return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    if (client.right - client.left != static_cast<LONG>(width) ||
        client.bottom - client.top != static_cast<LONG>(height)) {
        rollback();
        return E_FAIL;
    }
    if (previous_rect) *previous_rect = prior;
    return S_OK;
}

}

namespace sgl {

HRESULT Presentation::initialize_configured(ID3D11Device* device, HWND window, const std::filesystem::path& path, std::string& error) {
    DisplayConfig config{};
    if (!display_config(window, path, config, error)) return E_INVALIDARG;
    const HRESULT hr = initialize(device, window, config.size.width, config.size.height, config.mode);
    if (FAILED(hr)) {
        error = hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) ?
            "The selected windowed resolution does not fit within the monitor work area; choose a lower resolution or borderless mode." :
            "Cannot initialize D3D11 presentation";
    }
    return hr;
}

Presentation::~Presentation() noexcept { close(); }

LRESULT CALLBACK Presentation::window_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference_data) noexcept {
    auto* presentation = reinterpret_cast<Presentation*>(reference_data);
    if (presentation && presentation->mode_ == DisplayMode::Borderless &&
        window == presentation->window_ && message == WM_WINDOWPOSCHANGING && lparam) {
        auto* position = reinterpret_cast<WINDOWPOS*>(lparam);
        if (!(position->flags & SWP_NOMOVE)) {
            position->x = presentation->output_rect_.left;
            position->y = presentation->output_rect_.top;
        }
        if (!(position->flags & SWP_NOSIZE)) {
            position->cx = presentation->output_rect_.right - presentation->output_rect_.left;
            position->cy = presentation->output_rect_.bottom - presentation->output_rect_.top;
        }
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void Presentation::close() noexcept {
    if (window_subclassed_ && window_ && IsWindow(window_)) {
        RemoveWindowSubclass(window_, window_subclass_proc, reinterpret_cast<UINT_PTR>(this));
    }
    if (mode_ == DisplayMode::Windowed && original_window_state_saved_ && window_ && IsWindow(window_))
        restore_window(window_, original_style_, original_extended_style_, original_window_rect_);
    window_subclassed_ = false;
    original_window_state_saved_ = false;
    window_ = nullptr;
    output_rect_ = {};
    target_.Reset();
    back_buffer_.Reset();
    swap_chain_.Reset();
    device_.Reset();
    render_width_ = render_height_ = output_width_ = output_height_ = 0;
    original_style_ = original_extended_style_ = 0;
    original_window_rect_ = {};
    mode_ = DisplayMode::Borderless;
}

HRESULT Presentation::initialize(ID3D11Device* device, HWND window, UINT width, UINT height, DisplayMode mode) noexcept {
    if (!device || !window || !IsWindow(window) || !width || !height || swap_chain_) return E_INVALIDARG;
    if (mode != DisplayMode::Windowed && mode != DisplayMode::Borderless) return E_INVALIDARG;
    LONG_PTR original_style = 0, original_extended_style = 0;
    HRESULT hr = get_window_value(window, GWL_STYLE, original_style);
    if (FAILED(hr)) return hr;
    hr = get_window_value(window, GWL_EXSTYLE, original_extended_style);
    if (FAILED(hr)) return hr;
    RECT original_window_rect = {};
    if (!GetWindowRect(window, &original_window_rect)) return HRESULT_FROM_WIN32(GetLastError());

    RECT output_rect = {};
    if (mode == DisplayMode::Borderless) {
        hr = size_window_to_monitor(window, output_rect);
    } else {
        hr = place_windowed_client(window, width, height);
    }
    if (FAILED(hr)) {
        restore_window(window, original_style, original_extended_style, original_window_rect);
        return hr;
    }
    RECT client = {};
    if (!GetClientRect(window, &client)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        restore_window(window, original_style, original_extended_style, original_window_rect);
        return hr;
    }
    const LONG client_width = client.right - client.left;
    const LONG client_height = client.bottom - client.top;
    if (client_width <= 0 || client_height <= 0) {
        restore_window(window, original_style, original_extended_style, original_window_rect);
        return E_INVALIDARG;
    }
    ComPtr<IDXGIDevice> dxgi_device;
    hr = device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    if (FAILED(hr)) { restore_window(window, original_style, original_extended_style, original_window_rect); return hr; }
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter(&adapter);
    if (FAILED(hr)) { restore_window(window, original_style, original_extended_style, original_window_rect); return hr; }
    ComPtr<IDXGIFactory> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { restore_window(window, original_style, original_extended_style, original_window_rect); return hr; }
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferDesc.Width = static_cast<UINT>(client_width);
    desc.BufferDesc.Height = static_cast<UINT>(client_height);
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<IDXGISwapChain> chain;
    hr = factory->CreateSwapChain(device, &desc, &chain);
    if (FAILED(hr)) { restore_window(window, original_style, original_extended_style, original_window_rect); return hr; }
    ComPtr<ID3D11Texture2D> back_buffer;
    hr = chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr)) { restore_window(window, original_style, original_extended_style, original_window_rect); return hr; }
    ComPtr<ID3D11RenderTargetView> target;
    hr = device->CreateRenderTargetView(back_buffer.Get(), nullptr, &target);
    if (FAILED(hr)) { restore_window(window, original_style, original_extended_style, original_window_rect); return hr; }
    window_ = window;
    output_rect_ = output_rect;
    if (!SetWindowSubclass(window_, window_subclass_proc, reinterpret_cast<UINT_PTR>(this),
        reinterpret_cast<DWORD_PTR>(this))) {
        window_ = nullptr;
        output_rect_ = {};
        restore_window(window, original_style, original_extended_style, original_window_rect);
        return E_FAIL;
    }
    window_subclassed_ = true;
    device_ = device;
    swap_chain_ = chain;
    back_buffer_ = back_buffer;
    target_ = target;
    render_width_ = width;
    render_height_ = height;
    output_width_ = static_cast<UINT>(client_width);
    output_height_ = static_cast<UINT>(client_height);
    original_style_ = original_style;
    original_extended_style_ = original_extended_style;
    original_window_rect_ = original_window_rect;
    original_window_state_saved_ = true;
    mode_ = mode;
    return S_OK;
}

HRESULT Presentation::set_render_size(UINT width, UINT height) noexcept {
    if (!swap_chain_ || !is_supported_display_size({width, height})) return E_INVALIDARG;
    if (mode_ == DisplayMode::Windowed) {
        HRESULT hr = place_windowed_client(window_, output_width_, output_height_);
        if (FAILED(hr)) return hr;
        if (width != output_width_ || height != output_height_) {
            hr = resize_windowed_output(width, height);
            if (FAILED(hr)) return hr;
        }
    }
    render_width_ = width;
    render_height_ = height;
    return S_OK;
}

HRESULT Presentation::create_back_buffer() noexcept {
    if (!device_ || !swap_chain_) return E_UNEXPECTED;
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr)) return hr;
    ComPtr<ID3D11RenderTargetView> target;
    hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &target);
    if (FAILED(hr)) return hr;
    back_buffer_ = back_buffer;
    target_ = target;
    return S_OK;
}

HRESULT Presentation::resize_windowed_output(UINT width, UINT height) noexcept {
    if (mode_ != DisplayMode::Windowed || !window_ || !swap_chain_ || !device_) return E_UNEXPECTED;
    ComPtr<ID3D11DeviceContext> context;
    device_->GetImmediateContext(&context);
    if (!context) return E_FAIL;
    RECT previous_rect = {};
    HRESULT hr = place_windowed_client(window_, width, height, &previous_rect);
    if (FAILED(hr)) return hr;

    const UINT previous_width = output_width_;
    const UINT previous_height = output_height_;
    context->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset();
    back_buffer_.Reset();
    hr = swap_chain_->ResizeBuffers(1, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (FAILED(hr)) {
        SetWindowPos(window_, nullptr, previous_rect.left, previous_rect.top,
            previous_rect.right - previous_rect.left, previous_rect.bottom - previous_rect.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        const HRESULT restore_hr = create_back_buffer();
        return FAILED(restore_hr) ? restore_hr : hr;
    }

    hr = create_back_buffer();
    if (FAILED(hr)) {
        target_.Reset();
        back_buffer_.Reset();
        const HRESULT rollback_hr = swap_chain_->ResizeBuffers(1, previous_width, previous_height,
            DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        SetWindowPos(window_, nullptr, previous_rect.left, previous_rect.top,
            previous_rect.right - previous_rect.left, previous_rect.bottom - previous_rect.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        const HRESULT restore_hr = FAILED(rollback_hr) ? rollback_hr : create_back_buffer();
        return FAILED(restore_hr) ? restore_hr : hr;
    }
    output_width_ = width;
    output_height_ = height;
    return S_OK;
}

HRESULT Presentation::draw(ID3D11DeviceContext* context, Compositor& compositor, ID3D11ShaderResourceView* source,
    Rect source_rect, Rect render_rect) noexcept {
    if (!context || !source || !target_) return E_INVALIDARG;
    ComPtr<ID3D11Device> context_device;
    context->GetDevice(&context_device);
    if (context_device.Get() != device_.Get()) return E_INVALIDARG;
    ScreenTransform transform;
    if (!ScreenTransform::create(render_width_, render_height_, output_width_, output_height_, transform))
        return E_INVALIDARG;
    const float clear[] = {0, 0, 0, 1};
    context->ClearRenderTargetView(target_.Get(), clear);
    return compositor.copy(context, target_.Get(), source, output_width_, output_height_, source_rect,
        transform.map(render_rect),
        {0, 0, static_cast<LONG>(output_width_), static_cast<LONG>(output_height_)});
}

HRESULT Presentation::present() noexcept {
    return swap_chain_ ? swap_chain_->Present(1, 0) : E_UNEXPECTED;
}

}
