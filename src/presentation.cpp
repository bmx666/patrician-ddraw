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

}

namespace sgl {

HRESULT Presentation::initialize_configured(ID3D11Device* device, HWND window, const std::filesystem::path& path, std::string& error) {
    DisplaySize size{};
    if (!display_config(window, path, size, error)) return E_INVALIDARG;
    const HRESULT hr = initialize(device, window, size.width, size.height);
    if (FAILED(hr)) error = "Cannot initialize D3D11 presentation";
    return hr;
}

Presentation::~Presentation() noexcept { close(); }

LRESULT CALLBACK Presentation::window_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR reference_data) noexcept {
    auto* presentation = reinterpret_cast<Presentation*>(reference_data);
    if (presentation && window == presentation->window_ && message == WM_WINDOWPOSCHANGING && lparam) {
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
    window_subclassed_ = false;
    window_ = nullptr;
    output_rect_ = {};
    target_.Reset();
    back_buffer_.Reset();
    swap_chain_.Reset();
    device_.Reset();
    render_width_ = render_height_ = output_width_ = output_height_ = 0;
}

HRESULT Presentation::initialize(ID3D11Device* device, HWND window, UINT width, UINT height) noexcept {
    if (!device || !window || !IsWindow(window) || !width || !height || swap_chain_) return E_INVALIDARG;
    RECT output_rect = {};
    HRESULT hr = size_window_to_monitor(window, output_rect);
    if (FAILED(hr)) return hr;
    RECT client = {};
    if (!GetClientRect(window, &client)) return HRESULT_FROM_WIN32(GetLastError());
    const LONG client_width = client.right - client.left;
    const LONG client_height = client.bottom - client.top;
    if (client_width <= 0 || client_height <= 0) return E_INVALIDARG;
    ComPtr<IDXGIDevice> dxgi_device;
    hr = device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter(&adapter);
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIFactory> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;
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
    if (FAILED(hr)) return hr;
    ComPtr<ID3D11Texture2D> back_buffer;
    hr = chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr)) return hr;
    ComPtr<ID3D11RenderTargetView> target;
    hr = device->CreateRenderTargetView(back_buffer.Get(), nullptr, &target);
    if (FAILED(hr)) return hr;
    window_ = window;
    output_rect_ = output_rect;
    if (!SetWindowSubclass(window_, window_subclass_proc, reinterpret_cast<UINT_PTR>(this),
        reinterpret_cast<DWORD_PTR>(this))) {
        window_ = nullptr;
        output_rect_ = {};
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
    return S_OK;
}

HRESULT Presentation::set_render_size(UINT width, UINT height) noexcept {
    if (!swap_chain_ || !is_supported_display_size({width, height})) return E_INVALIDARG;
    render_width_ = width;
    render_height_ = height;
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
