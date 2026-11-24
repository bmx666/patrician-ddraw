#pragma once

#include "compositor.hpp"

#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <string>

namespace sgl {

// Renderer work between two presents: compositor draws, the instanced draw calls and queue flushes that recorded
// them, GPU-to-CPU readbacks, CPU-to-GPU texture uploads and the bytes those two transfers moved.
struct FrameCounters {
    unsigned draws = 0;
    unsigned draw_calls = 0;
    unsigned batches = 0;
    unsigned readbacks = 0;
    unsigned uploads = 0;
    unsigned long long transfer_bytes = 0;
    // QueryPerformanceCounter ticks spent in readbacks (including the GPU wait) and in uploads.
    long long readback_ticks = 0;
    long long upload_ticks = 0;
};

// Frame statistics drawn into the top-left corner of the presented image: frames per second, average frame time
// and the longest frame of the last half second, measured between presents.
class FpsOverlay {
public:
    void frame_presented(const FrameCounters& counters = {}) noexcept;
    HRESULT draw(ID3D11Device* device, ID3D11DeviceContext* context, Compositor& compositor,
        ID3D11RenderTargetView* target, UINT target_width, UINT target_height) noexcept;
    void reset() noexcept;
    const std::string& text() const noexcept { return text_; }

    static constexpr UINT max_characters = 100;
    static constexpr UINT glyph_width = 5;
    static constexpr UINT glyph_height = 7;
    static constexpr UINT padding = 2;
    static constexpr UINT texture_width = padding * 2 + max_characters * (glyph_width + 1);
    static constexpr UINT texture_height = padding * 2 + glyph_height;
    static constexpr std::uint32_t background = 0xff101010;
    static constexpr std::uint32_t foreground = 0xffffff00;

private:
    void update_text(double fps, double average_ms, double longest_ms) noexcept;
    FrameCounters totals_{};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    std::array<std::uint32_t, texture_width * texture_height> pixels_{};
    std::string text_ = "-- fps";
    UINT text_width_ = 0;
    bool pixels_dirty_ = true;
    long long frequency_ = 0;
    long long previous_ = 0;
    long long window_start_ = 0;
    long long longest_ = 0;
    unsigned frames_ = 0;
};

}
