#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <optional>

namespace sgl {

struct Rect {
    float left;
    float top;
    float right;
    float bottom;
};

// PresentSharp: final output scaling. Nearest texel, blended only across the one output pixel that
// straddles a texel edge, so non-integer scale factors keep even pixel widths without blurring.
enum class Sampling { Center, SglFixedPoint, SglBilinear, PresentSharp };
// FillAlpha reproduces sgl_FillSolidRect's translucent fill: (source * a >> 8) + (background * (255 - a) >> 8), with a
// taken from the modulation alpha.
enum class BlendMode { Opaque, Alpha, Multiply, MultiplyModulated, Multiply2, Additive, AdditiveModulated, AlphaMask, FillAlpha };

class Compositor {
public:
    HRESULT initialize(ID3D11Device* device) noexcept;
    HRESULT copy(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        ID3D11ShaderResourceView* source, UINT target_width, UINT target_height,
        Rect source_rect, Rect destination_rect, RECT clip, Sampling sampling = Sampling::Center,
        std::optional<std::uint32_t> color_key = std::nullopt,
        ID3D11ShaderResourceView* background = nullptr, const char** failure_stage = nullptr,
        std::uint32_t modulation_color = 0xffffffff, BlendMode blend = BlendMode::Opaque) noexcept;
    HRESULT copy_indexed(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        ID3D11ShaderResourceView* indices, ID3D11ShaderResourceView* palette,
        UINT target_width, UINT target_height, Rect source_rect, Rect destination_rect, RECT clip,
        std::optional<std::uint32_t> color_key = std::nullopt,
        Sampling sampling = Sampling::Center, const char** failure_stage = nullptr,
        std::uint32_t modulation_color = 0xffffffff,
        ID3D11ShaderResourceView* background = nullptr, BlendMode blend = BlendMode::Opaque) noexcept;
    HRESULT copy_masked_indexed(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        ID3D11ShaderResourceView* indices, ID3D11ShaderResourceView* palette,
        ID3D11ShaderResourceView* mask, ID3D11ShaderResourceView* background,
        UINT target_width, UINT target_height, Rect source_rect, Rect destination_rect, RECT clip,
        std::uint32_t modulation_color = 0xffffffff, const char** failure_stage = nullptr) noexcept;
    // Call after binding or unbinding pipeline state outside the compositor.
    void invalidate_renderer_bindings() noexcept;

    // The city water effect of the game (FUN_0059e8e0) for a list of tiles. For every target pixel (x, y) with
    // h the height byte of that pixel, dx = h(x + 1, y) - h(x, y) and dy = h(x, y + 1) - h(x, y), the color channels
    // become those of the pixel (x + 2 * (dx / 4), y + dy / 2) minus dy, saturated to 0..255, with C integer division
    // and the read position clamped to the limit size; the target alpha is kept. Like the original, which works in
    // place tile by tile and row by row, a read of a pixel written earlier in that order sees the written value.
    struct WaterDraw {
        ID3D11RenderTargetView* target = nullptr;
        UINT target_width = 0;
        UINT target_height = 0;
        // Pixels of the target before the effect; the target itself is not read.
        ID3D11ShaderResourceView* source = nullptr;
        // Distinct target rectangles inside area, in the original's order.
        const RECT* tiles = nullptr;
        UINT tile_count = 0;
        RECT area{};
        // Height byte of pixel (area.left, area.top); rows of height_pitch bytes. Columns area.left..area.right and
        // rows area.top..area.bottom, both inclusive, are read.
        const std::uint8_t* heights = nullptr;
        UINT height_pitch = 0;
        UINT limit_width = 0;
        UINT limit_height = 0;
    };
    // Records the effect with its own pipeline state and leaves no shader resources bound.
    HRESULT draw_water(ID3D11DeviceContext* context, const WaterDraw& draw) noexcept;

    // A validated draw with its pipeline inputs resolved, recorded later by record(). The views are not referenced;
    // the caller keeps them alive until the draw is recorded.
    struct PreparedDraw {
        ID3D11RenderTargetView* target = nullptr;
        UINT target_width = 0;
        UINT target_height = 0;
        RECT clip{};
        ID3D11PixelShader* pixel_shader = nullptr;
        ID3D11ShaderResourceView* resources[5] = {};
        std::uint8_t constants[96] = {};
        bool visible = false;
    };

private:
    friend class SurfaceRenderer;

    struct RendererSource {
        UINT width;
        UINT height;
        DXGI_FORMAT format;
    };

    HRESULT copy_impl(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* palette,
        ID3D11ShaderResourceView* mask,
        UINT target_width, UINT target_height, Rect source_rect, Rect destination_rect, RECT clip,
        std::optional<std::uint32_t> color_key, Sampling sampling, ID3D11ShaderResourceView* background,
        std::uint32_t modulation_color, BlendMode blend, const char** failure_stage,
        const RendererSource* renderer_source) noexcept;
    // Validates a draw and resolves its shader, resources and constants without touching the pipeline.
    HRESULT prepare(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* palette,
        ID3D11ShaderResourceView* mask,
        UINT target_width, UINT target_height, Rect source_rect, Rect destination_rect, RECT clip,
        std::optional<std::uint32_t> color_key, Sampling sampling, ID3D11ShaderResourceView* background,
        std::uint32_t modulation_color, BlendMode blend, const char** failure_stage,
        const RendererSource* renderer_source, PreparedDraw& prepared) noexcept;
    // Records a prepared draw. Renderer-owned draws keep their bindings cached between draws; other draws unbind
    // their shader resources afterwards.
    void record(ID3D11DeviceContext* context, const PreparedDraw& prepared, bool renderer_owned) noexcept;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> keyed_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> alpha_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> multiply_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> multiply_modulated_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> multiply2_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> fill_alpha_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> additive_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> additive_modulated_pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> indexed_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> keyed_indexed_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> alpha_indexed_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> multiply_indexed_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> multiply_modulated_indexed_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> additive_indexed_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> additive_modulated_indexed_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> alpha_masked_indexed_shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> present_sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    ID3D11DeviceContext* renderer_state_context_ = nullptr;
    ID3D11PixelShader* renderer_pixel_shader_ = nullptr;
    // Renderer-owned draws also keep target, viewport and shader resources bound between draws.
    ID3D11RenderTargetView* renderer_target_ = nullptr;
    UINT renderer_viewport_width_ = 0;
    UINT renderer_viewport_height_ = 0;
    ID3D11ShaderResourceView* renderer_resources_[5] = {};

    Microsoft::WRL::ComPtr<ID3D11VertexShader> water_vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> water_order_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> water_start_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> water_jump_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> water_finish_shader_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> water_blend_state_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> water_constants_;
    // Tile rectangles, grown on demand.
    Microsoft::WRL::ComPtr<ID3D11Buffer> water_tiles_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> water_tiles_view_;
    UINT water_tile_capacity_ = 0;
    // Per-pixel working textures in the coordinates of the written area, grown on demand: height bytes (one more row
    // and column), write order, and two pointer-jumping states.
    struct WaterTexture {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    };
    struct WaterArea {
        UINT width = 0;
        UINT height = 0;
        WaterTexture heights;
        WaterTexture order;
        WaterTexture states[2];
    } water_area_;
    HRESULT reserve_water_area(UINT width, UINT height) noexcept;
};

}
