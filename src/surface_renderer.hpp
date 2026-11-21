#pragma once

#include "compositor.hpp"
#include "image_decoder.hpp"
#include "presentation.hpp"
#include <unordered_map>

namespace sgl {

class SurfaceRenderer {
public:
    HRESULT initialize() noexcept;
    HRESULT draw(void* source_id, const DecodedImage& source, void* target_id,
        DecodedImage& target, Rect source_rect, Rect target_rect, RECT clip,
        std::optional<std::uint32_t> key, BlendMode blend, const char** failure_stage = nullptr,
        std::uint32_t modulation_color = 0xffffffff, Sampling sampling = Sampling::SglFixedPoint) noexcept;
    HRESULT draw_masked(void* source_id, const DecodedImage& source, void* mask_id,
        const DecodedImage& mask, void* target_id, DecodedImage& target, Rect source_rect,
        Rect target_rect, RECT clip, std::uint32_t modulation_color = 0xffffffff,
        const char** failure_stage = nullptr) noexcept;
    HRESULT synchronize_to_cpu(void* id, DecodedImage& image) noexcept;
    void mark_cpu_modified(void* id) noexcept;
    // True when the GPU copy of a surface is newer than its CPU pixels, so CPU access would need a readback.
    bool gpu_holds_newer_pixels(void* id) const noexcept;
    void mark_palette_modified(void* id) noexcept;
    void forget(void* id) noexcept;
    HRESULT open_screen(HWND window, const std::filesystem::path& config, std::string& error);
    HRESULT set_screen_size(DisplaySize size) noexcept;
    void close_screen() noexcept;
    void shutdown() noexcept;
    DisplaySize screen_size() const noexcept;
    DisplaySize output_size() const noexcept;
    DisplayMode display_mode() const noexcept;
    std::optional<unsigned> max_fps() const noexcept;
    HRESULT present(void* id, const DecodedImage& image) noexcept;

private:
    struct Surface {
        D3D11_TEXTURE2D_DESC description = {};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> background;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> background_view;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> palette;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> palette_view;
        bool cpu_needs_upload = true;
        bool cpu_needs_readback = false;
        bool palette_needs_upload = true;
        // Bounds of the GPU writes not yet copied to the CPU pixels; readback copies only this region.
        RECT gpu_dirty{};
    };
    HRESULT surface(void* id, const DecodedImage& image, Surface*& result) noexcept;
    HRESULT upload_cpu_image(Surface& surface, const DecodedImage& image) noexcept;
    HRESULT draw_impl(void* source_id, const DecodedImage& source, void* mask_id,
        const DecodedImage* mask, void* target_id, DecodedImage& target, Rect source_rect,
        Rect target_rect, RECT clip, std::optional<std::uint32_t> key, BlendMode blend,
        const char** failure_stage, std::uint32_t modulation_color, Sampling sampling) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Compositor compositor_;
    Presentation presentation_;
    std::unordered_map<void*, Surface> surfaces_;
    FrameCounters counters_{};
    ID3D11Resource* bound_target_ = nullptr;
};

}
