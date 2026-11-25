#pragma once

#include "compositor.hpp"
#include "image_decoder.hpp"
#include "presentation.hpp"
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace sgl {

class SurfaceRenderer {
public:
    HRESULT initialize() noexcept;
    // Step of the last failed initialize(), for the error message.
    const char* init_failure_stage() const noexcept { return init_failure_stage_; }
    HRESULT draw(void* source_id, const DecodedImage& source, void* target_id,
        DecodedImage& target, Rect source_rect, Rect target_rect, RECT clip,
        std::optional<std::uint32_t> key, BlendMode blend, const char** failure_stage = nullptr,
        std::uint32_t modulation_color = 0xffffffff, Sampling sampling = Sampling::SglFixedPoint) noexcept;
    HRESULT draw_masked(void* source_id, const DecodedImage& source, void* mask_id,
        const DecodedImage& mask, void* target_id, DecodedImage& target, Rect source_rect,
        Rect target_rect, RECT clip, std::uint32_t modulation_color = 0xffffffff,
        const char** failure_stage = nullptr) noexcept;
    // Copies a region of a surface onto itself on the GPU: the source region is first copied into a snapshot texture
    // of the surface, so overlapping regions copy as from the pixels before the call, without a CPU readback.
    HRESULT copy_within(void* id, DecodedImage& image, Rect source_rect, Rect target_rect, RECT clip,
        const char** failure_stage = nullptr) noexcept;
    // Applies the city water effect (Compositor::WaterDraw) to the tiles of a BGRA surface on the GPU, reading the
    // displaced pixels from a snapshot of the surface taken before the call. heights points at the height byte of
    // pixel (area.left, area.top); tiles lie inside area, and area inside the surface. The CPU pixels of the tiles
    // become stale like after any draw.
    HRESULT draw_water(void* id, DecodedImage& image, const RECT* tiles, std::size_t tile_count, RECT area,
        const std::uint8_t* heights, UINT height_pitch, UINT limit_width, UINT limit_height) noexcept;
    HRESULT synchronize_to_cpu(void* id, DecodedImage& image) noexcept;
    // Partial transfers for CPU work confined to a known region of a GPU-current BGRA surface: the readback copies
    // the region's GPU-written pixels to the CPU; the upload copies the region's CPU pixels to the GPU. Neither
    // changes which copy is authoritative for the rest of the surface.
    HRESULT synchronize_region_to_cpu(void* id, DecodedImage& image, RECT region) noexcept;
    HRESULT upload_region(void* id, const DecodedImage& image, RECT region) noexcept;
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
    // Exact blending copies the target under every translucent draw and reproduces the original integer formulas;
    // otherwise hardware blend states combine the draws with the target in one pass. open_screen applies the
    // [display] exact_blend setting.
    void set_exact_blend(bool exact) noexcept;
    // Names the SGL export and the game address that called it for the readbacks made until the next call; null
    // clears it. In diagnostics builds with show_fps, readbacks are summed per site, caller and surface size into
    // sgl_readbacks.log next to high_res.ini every two seconds.
    void set_readback_site(const char* site, std::uintptr_t caller) noexcept;

private:
    struct Surface {
        D3D11_TEXTURE2D_DESC description = {};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> background;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> background_view;
        // Source pixels of copy_within, kept apart from background because background copies of queued draws are
        // recorded ahead of the draws and would overwrite a snapshot that a queued draw still reads.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> snapshot;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> snapshot_view;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> palette;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> palette_view;
        bool cpu_needs_upload = true;
        bool cpu_needs_readback = false;
        bool palette_needs_upload = true;
        // Bounds of the GPU writes not yet copied to the CPU pixels; readback copies only this region.
        RECT gpu_dirty{};
    };
    // A queued draw's target bookkeeping; its prepared draw is at the same index of pending_draws_. Draws to one
    // target are queued so that the background copies of blended draws are recorded together ahead of the draws
    // instead of alternating with them; every copy and draw stalls on the one before.
    struct PendingDraw {
        ID3D11Texture2D* texture = nullptr;
        ID3D11Texture2D* background = nullptr;
        D3D11_BOX copy{};
        RECT written{};
    };
    // Records queued draws: first every background copy, then the draws in submission order. A copy taken at the
    // start of the batch equals one taken right before its draw because no earlier draw of the batch writes the copied
    // region (see draw_impl). Must run before anything else reads or writes the queued resources.
    void flush_pending() noexcept;
    // flush_pending, counted under reason when the queue holds draws.
    void flush_pending(unsigned& reason) noexcept;
    // True when a queued draw reads or writes the surface, so changing its textures must flush the queue first.
    bool pending_uses(const Surface& surface) const noexcept;
    struct ReadbackSource {
        const char* site = nullptr;
        std::uintptr_t caller = 0;
        UINT width = 0;
        UINT height = 0;
        unsigned count = 0;
        unsigned long long bytes = 0;
        long long ticks = 0;
    };
    void trace_readback(const Surface& surface, unsigned long long bytes, long long ticks) noexcept;
    // Appends the summed readbacks to the log and starts a new window once two seconds have passed.
    void write_readback_trace() noexcept;
    HRESULT surface(void* id, const DecodedImage& image, Surface*& result) noexcept;
    HRESULT upload_cpu_image(Surface& surface, const DecodedImage& image) noexcept;
    HRESULT create_snapshot(Surface& surface) noexcept;
    HRESULT draw_impl(void* source_id, const DecodedImage& source, void* mask_id,
        const DecodedImage* mask, void* target_id, DecodedImage& target, Rect source_rect,
        Rect target_rect, RECT clip, std::optional<std::uint32_t> key, BlendMode blend,
        const char** failure_stage, std::uint32_t modulation_color, Sampling sampling,
        bool self_copy = false) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Compositor compositor_;
    const char* init_failure_stage_ = "";
    Presentation presentation_;
    std::unordered_map<void*, Surface> surfaces_;
    FrameCounters counters_{};
    ID3D11Resource* bound_target_ = nullptr;
    std::vector<PendingDraw> pending_;
    std::vector<Compositor::PreparedDraw> pending_draws_;
    ID3D11Texture2D* pending_target_ = nullptr;
    RECT pending_bounds_{};
    bool exact_blend_ = false;
    const char* readback_site_ = nullptr;
    std::uintptr_t readback_caller_ = 0;
    std::filesystem::path readback_log_;
    std::vector<ReadbackSource> readback_sources_;
    long long readback_window_start_ = 0;
    unsigned readback_frames_ = 0;
};

}
