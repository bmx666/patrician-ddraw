#pragma once

#include "compositor.hpp"
#include "display_config.hpp"
#include <dxgi.h>

namespace sgl {

class Presentation {
public:
    Presentation() = default;
    ~Presentation() noexcept;
    Presentation(const Presentation&) = delete;
    Presentation& operator=(const Presentation&) = delete;

    HRESULT initialize(ID3D11Device* device, HWND window, UINT width, UINT height,
        DisplayMode mode = DisplayMode::Borderless) noexcept;
    HRESULT initialize_configured(ID3D11Device* device, HWND window, const std::filesystem::path& path, std::string& error);
    HRESULT set_render_size(UINT width, UINT height) noexcept;
    void close() noexcept;
    HRESULT draw(ID3D11DeviceContext* context, Compositor& compositor, ID3D11ShaderResourceView* source,
        Rect source_rect, Rect render_rect) noexcept;
    HRESULT present() noexcept;
    ID3D11Texture2D* back_buffer() const noexcept { return back_buffer_.Get(); }
    DisplaySize render_size() const noexcept { return {render_width_, render_height_}; }
    DisplaySize output_size() const noexcept { return {output_width_, output_height_}; }
    DisplayMode display_mode() const noexcept { return mode_; }

private:
    static LRESULT CALLBACK window_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass_id, DWORD_PTR reference_data) noexcept;
    HRESULT create_back_buffer() noexcept;
    HRESULT resize_windowed_output(UINT width, UINT height) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    UINT render_width_ = 0;
    UINT render_height_ = 0;
    UINT output_width_ = 0;
    UINT output_height_ = 0;
    HWND window_ = nullptr;
    RECT output_rect_{};
    LONG_PTR windowed_style_ = 0;
    LONG_PTR windowed_extended_style_ = 0;
    LONG_PTR original_style_ = 0;
    LONG_PTR original_extended_style_ = 0;
    RECT original_window_rect_{};
    DisplayMode mode_ = DisplayMode::Borderless;
    bool window_subclassed_ = false;
    bool original_window_state_saved_ = false;
    bool window_change_in_progress_ = false;
};

}
