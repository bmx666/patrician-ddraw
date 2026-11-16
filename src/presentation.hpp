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

    HRESULT initialize(ID3D11Device* device, HWND window, UINT width, UINT height) noexcept;
    HRESULT initialize_configured(ID3D11Device* device, HWND window, const std::filesystem::path& path, std::string& error);
    HRESULT set_render_size(UINT width, UINT height) noexcept;
    void close() noexcept;
    HRESULT draw(ID3D11DeviceContext* context, Compositor& compositor, ID3D11ShaderResourceView* source,
        Rect source_rect, Rect render_rect) noexcept;
    HRESULT present() noexcept;
    ID3D11Texture2D* back_buffer() const noexcept { return back_buffer_.Get(); }
    DisplaySize render_size() const noexcept { return {render_width_, render_height_}; }
    DisplaySize output_size() const noexcept { return {output_width_, output_height_}; }

private:
    static LRESULT CALLBACK window_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass_id, DWORD_PTR reference_data) noexcept;

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
    bool window_subclassed_ = false;
};

}
