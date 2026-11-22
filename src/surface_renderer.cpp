#include "surface_renderer.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
HRESULT as_bgra(const sgl::DecodedImage& image, std::vector<std::uint8_t>& converted,
    const std::uint8_t*& pixels, UINT& pitch) noexcept {
    pixels = image.pixels.data();
    pitch = image.pitch;
    if (image.format != sgl::ImageFormat::Bgr24) return S_OK;
    if (image.width > UINT_MAX / 4 ||
        image.pitch < static_cast<std::uint64_t>(image.width) * 3 ||
        static_cast<std::uint64_t>(image.pitch) * image.height > image.pixels.size()) return E_INVALIDARG;
    const std::uint64_t converted_size = static_cast<std::uint64_t>(image.width) * image.height * 4;
    if (converted_size > SIZE_MAX) return E_OUTOFMEMORY;
    try {
        converted.resize(static_cast<std::size_t>(converted_size));
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return E_OUTOFMEMORY;
    }
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const auto* source_row = image.pixels.data() + static_cast<std::size_t>(y) * image.pitch;
        auto* target_row = converted.data() + static_cast<std::size_t>(y) * image.width * 4;
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const auto* source_pixel = source_row + static_cast<std::size_t>(x) * 3;
            auto* target_pixel = target_row + static_cast<std::size_t>(x) * 4;
            target_pixel[0] = source_pixel[0];
            target_pixel[1] = source_pixel[1];
            target_pixel[2] = source_pixel[2];
            target_pixel[3] = 0xff;
        }
    }
    pixels = converted.data();
    pitch = image.width * 4;
    return S_OK;
}
}

namespace sgl {

HRESULT SurfaceRenderer::initialize() noexcept {
    if (device_) return S_OK;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(result)) return result;
    result = compositor_.initialize(device.Get());
    if (FAILED(result)) return result;
    device_ = device;
    context_ = context;
    return S_OK;
}

HRESULT SurfaceRenderer::surface(void* id, const DecodedImage& image, Surface*& result) noexcept {
    const auto found = surfaces_.find(id);
    if (found != surfaces_.end()) {
        const auto expected_format = image.format == ImageFormat::Indexed8 ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_B8G8R8A8_UNORM;
        if (found->second.description.Width != image.width || found->second.description.Height != image.height ||
            found->second.description.Format != expected_format) return E_INVALIDARG;
        result = &found->second;
        return S_OK;
    }
    Surface next;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = image.width;
    desc.Height = image.height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = image.format == ImageFormat::Indexed8 ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (image.format != ImageFormat::Indexed8) desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &next.texture);
    if (FAILED(hr)) return hr;
    next.description = desc;
    hr = device_->CreateShaderResourceView(next.texture.Get(), nullptr, &next.view);
    if (FAILED(hr)) return hr;
    if (image.format == ImageFormat::Indexed8) {
        desc.Width = 256;
        desc.Height = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        hr = device_->CreateTexture2D(&desc, nullptr, &next.palette);
        if (FAILED(hr)) return hr;
        hr = device_->CreateShaderResourceView(next.palette.Get(), nullptr, &next.palette_view);
    } else {
        hr = device_->CreateRenderTargetView(next.texture.Get(), nullptr, &next.target);
        if (FAILED(hr)) return hr;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device_->CreateTexture2D(&desc, nullptr, &next.staging);
    }
    if (FAILED(hr)) return hr;
    try {
        result = &surfaces_.emplace(id, std::move(next)).first->second;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    return S_OK;
}

namespace {
long long performance_counter() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}
}

HRESULT SurfaceRenderer::upload_cpu_image(Surface& surface, const DecodedImage& image) noexcept {
    // Queued draws may read or write this texture; they were submitted before the upload.
    if (surface.cpu_needs_upload || (surface.palette && surface.palette_needs_upload && !image.palette.empty()))
        flush_pending();
    if (surface.cpu_needs_upload) {
        const long long started = performance_counter();
        std::vector<std::uint8_t> converted;
        const std::uint8_t* pixels = nullptr;
        UINT pitch = 0;
        const HRESULT hr = as_bgra(image, converted, pixels, pitch);
        if (FAILED(hr)) return hr;
        context_->UpdateSubresource(surface.texture.Get(), 0, nullptr, pixels, pitch, 0);
        ++counters_.uploads;
        counters_.transfer_bytes += static_cast<unsigned long long>(pitch) * image.height;
        counters_.upload_ticks += performance_counter() - started;
        surface.cpu_needs_upload = false;
        surface.cpu_needs_readback = false;
        surface.gpu_dirty = {};
    }
    if (surface.palette && surface.palette_needs_upload && !image.palette.empty()) {
        if (image.palette.size() != 1024) return E_INVALIDARG;
        context_->UpdateSubresource(surface.palette.Get(), 0, nullptr, image.palette.data(), 1024, 0);
        surface.palette_needs_upload = false;
    }
    return S_OK;
}

HRESULT SurfaceRenderer::draw(void* source_id, const DecodedImage& source, void* target_id,
    DecodedImage& target, Rect source_rect, Rect target_rect, RECT clip, std::optional<std::uint32_t> key, BlendMode blend,
    const char** failure_stage, std::uint32_t modulation_color, Sampling sampling) noexcept {
    return draw_impl(source_id, source, nullptr, nullptr, target_id, target, source_rect,
        target_rect, clip, key, blend, failure_stage, modulation_color, sampling);
}

HRESULT SurfaceRenderer::draw_masked(void* source_id, const DecodedImage& source, void* mask_id,
    const DecodedImage& mask, void* target_id, DecodedImage& target, Rect source_rect,
    Rect target_rect, RECT clip, std::uint32_t modulation_color, const char** failure_stage) noexcept {
    return draw_impl(source_id, source, mask_id, &mask, target_id, target, source_rect,
        target_rect, clip, std::nullopt, BlendMode::AlphaMask, failure_stage, modulation_color, Sampling::SglFixedPoint);
}

HRESULT SurfaceRenderer::draw_impl(void* source_id, const DecodedImage& source, void* mask_id,
    const DecodedImage* mask, void* target_id, DecodedImage& target, Rect source_rect,
    Rect target_rect, RECT clip, std::optional<std::uint32_t> key, BlendMode blend,
    const char** failure_stage, std::uint32_t modulation_color, Sampling sampling) noexcept {
    if (failure_stage) *failure_stage = "argument validation";
    if (!source_id || !target_id || source_id == target_id ||
        (mask && mask_id == target_id) ||
        ((mask == nullptr) != (mask_id == nullptr)) ||
        ((blend == BlendMode::AlphaMask) != (mask != nullptr)) ||
        (target.format != ImageFormat::Bgra32 && target.format != ImageFormat::Bgr24) ||
        source.pixels.empty() || target.pixels.empty() ||
        source.pitch < static_cast<std::uint64_t>(source.width) * bytes_per_pixel(source.format) ||
        target.pitch < static_cast<std::uint64_t>(target.width) * bytes_per_pixel(target.format) ||
        static_cast<std::uint64_t>(source.pitch) * source.height > source.pixels.size() ||
        static_cast<std::uint64_t>(target.pitch) * target.height > target.pixels.size() ||
        (source.format == ImageFormat::Indexed8 && source.palette.size() != 1024) ||
        (mask && (source.format != ImageFormat::Indexed8 || mask->format != ImageFormat::Indexed8 ||
            source.width != mask->width || source.height != mask->height || source.pitch != mask->pitch ||
            (!mask->palette.empty() && mask->palette.size() != 1024) || mask->pixels.empty() ||
            mask->pitch < static_cast<std::uint64_t>(mask->width) ||
            static_cast<std::uint64_t>(mask->pitch) * mask->height > mask->pixels.size())) ||
        (blend != BlendMode::Opaque && key &&
            !(source.format == ImageFormat::Indexed8 && blend == BlendMode::Alpha))) return E_INVALIDARG;
    if (failure_stage) *failure_stage = "D3D11 initialization";
    HRESULT hr = initialize();
    if (FAILED(hr)) return hr;
    Surface* src = nullptr;
    Surface* mask_surface = nullptr;
    Surface* dst = nullptr;
    if (failure_stage) *failure_stage = "source surface creation";
    hr = surface(source_id, source, src);
    if (FAILED(hr)) return hr;
    if (mask) {
        if (failure_stage) *failure_stage = "mask surface creation";
        hr = surface(mask_id, *mask, mask_surface);
        if (FAILED(hr)) return hr;
    }
    if (failure_stage) *failure_stage = "destination surface creation";
    hr = surface(target_id, target, dst);
    if (FAILED(hr)) return hr;
    // Insertion can rehash the map, so reacquire both references.
    src = &surfaces_.at(source_id);
    if (mask) mask_surface = &surfaces_.at(mask_id);
    dst = &surfaces_.at(target_id);
    // Queued draws belong to one target; recording them before switching keeps the GPU order intact.
    if (pending_target_ && pending_target_ != dst->texture.Get()) flush_pending();
    // The last recorded draw leaves its target bound. Unbind it only when this draw reads it; binding a bound render
    // target as a shader resource would silently null the input, while re-binding per draw costs driver time.
    if (bound_target_ && (bound_target_ == src->texture.Get() || (mask && bound_target_ == mask_surface->texture.Get()))) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        bound_target_ = nullptr;
        compositor_.invalidate_renderer_bindings();
    }
    if (failure_stage) *failure_stage = "source texture upload";
    hr = upload_cpu_image(*src, source);
    if (FAILED(hr)) return hr;
    if (mask) {
        if (failure_stage) *failure_stage = "mask texture upload";
        hr = upload_cpu_image(*mask_surface, *mask);
        if (FAILED(hr)) return hr;
    }
    if (failure_stage) *failure_stage = "destination texture upload";
    hr = upload_cpu_image(*dst, target);
    if (FAILED(hr)) return hr;
    const LONG width = static_cast<LONG>(target.width), height = static_cast<LONG>(target.height);
    const auto bound = [](float value, LONG low, LONG high, bool up) {
        const float rounded = up ? std::ceil(value) : std::floor(value);
        return std::clamp(static_cast<LONG>(std::clamp(rounded, -1.0e9f, 1.0e9f)), low, high);
    };
    // Pixels this draw can write, which for blended draws are also the background pixels it reads.
    const RECT written{std::max(bound(target_rect.left, 0, width, false), std::clamp<LONG>(clip.left, 0, width)),
        std::max(bound(target_rect.top, 0, height, false), std::clamp<LONG>(clip.top, 0, height)),
        std::min(bound(target_rect.right, 0, width, true), std::clamp<LONG>(clip.right, 0, width)),
        std::min(bound(target_rect.bottom, 0, height, true), std::clamp<LONG>(clip.bottom, 0, height))};
    const bool writes = written.left < written.right && written.top < written.bottom;
    if (blend != BlendMode::Opaque && !dst->background) {
        if (failure_stage) *failure_stage = "alpha background allocation";
        D3D11_TEXTURE2D_DESC desc = dst->description;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> background;
        ComPtr<ID3D11ShaderResourceView> view;
        hr = device_->CreateTexture2D(&desc, nullptr, &background);
        if (FAILED(hr)) return hr;
        hr = device_->CreateShaderResourceView(background.Get(), nullptr, &view);
        if (FAILED(hr)) return hr;
        dst->background = background;
        dst->background_view = view;
    }
    if (failure_stage) *failure_stage = "compositor draw";
    const Compositor::RendererSource renderer_source{source.width, source.height,
        source.format == ImageFormat::Indexed8 ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_B8G8R8A8_UNORM};
    PendingDraw pending;
    if (mask) {
        hr = compositor_.prepare(context_.Get(), dst->target.Get(), src->view.Get(),
            src->palette_view.Get(), mask_surface->view.Get(), target.width, target.height,
            source_rect, target_rect, clip, std::nullopt, Sampling::SglFixedPoint,
            dst->background_view.Get(), modulation_color, BlendMode::AlphaMask, failure_stage, &renderer_source,
            pending.draw);
    } else {
        hr = compositor_.prepare(context_.Get(), dst->target.Get(), src->view.Get(),
            source.format == ImageFormat::Indexed8 ? src->palette_view.Get() : nullptr, nullptr,
            target.width, target.height, source_rect, target_rect, clip, key, sampling,
            blend != BlendMode::Opaque ? dst->background_view.Get() : nullptr,
            modulation_color, blend, failure_stage, &renderer_source, pending.draw);
    }
    if (FAILED(hr)) return hr;
    ++counters_.draws;
    if (pending.draw.visible) {
        const auto overlaps = [](const RECT& a, const RECT& b) {
            return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
        };
        if (blend != BlendMode::Opaque && writes) {
            // The copy is recorded ahead of the queued draws, so it must not cover pixels one of them writes.
            if (!pending_.empty() && overlaps(pending_bounds_, written) &&
                std::any_of(pending_.begin(), pending_.end(), [&](const PendingDraw& queued) { return overlaps(queued.written, written); }))
                flush_pending();
            pending.background = dst->background.Get();
            pending.copy = {static_cast<UINT>(written.left), static_cast<UINT>(written.top), 0,
                static_cast<UINT>(written.right), static_cast<UINT>(written.bottom), 1};
        }
        pending.texture = dst->texture.Get();
        pending.written = writes ? written : RECT{};
        try {
            if (pending_.capacity() == 0) pending_.reserve(1024);
            pending_.push_back(pending);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
        pending_target_ = pending.texture;
        if (writes) {
            pending_bounds_ = pending_.size() == 1 || pending_bounds_.left >= pending_bounds_.right ? written :
                RECT{std::min(pending_bounds_.left, written.left), std::min(pending_bounds_.top, written.top),
                    std::max(pending_bounds_.right, written.right), std::max(pending_bounds_.bottom, written.bottom)};
        }
        if (pending_.size() >= 1024) flush_pending();
    }
    if (writes) {
        auto& dirty = dst->gpu_dirty;
        if (dirty.left >= dirty.right || dirty.top >= dirty.bottom) dirty = written;
        else dirty = {std::min(dirty.left, written.left), std::min(dirty.top, written.top),
            std::max(dirty.right, written.right), std::max(dirty.bottom, written.bottom)};
    }
    dst->cpu_needs_upload = false;
    dst->cpu_needs_readback = true;
    return S_OK;
}

void SurfaceRenderer::flush_pending() noexcept {
    if (pending_.empty()) {
        pending_target_ = nullptr;
        return;
    }
    for (const auto& queued : pending_) {
        if (queued.background) {
            context_->CopySubresourceRegion(queued.background, 0, queued.copy.left, queued.copy.top, 0,
                queued.texture, 0, &queued.copy);
        }
    }
    for (const auto& queued : pending_) compositor_.record(context_.Get(), queued.draw, true);
    bound_target_ = pending_target_;
    pending_.clear();
    pending_target_ = nullptr;
    pending_bounds_ = {};
}

HRESULT SurfaceRenderer::synchronize_to_cpu(void* id, DecodedImage& image) noexcept {
    if (!id) return E_INVALIDARG;
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end()) return S_OK;
    auto& target = found->second;
    if (!target.cpu_needs_readback || target.cpu_needs_upload) return S_OK;
    if (pending_target_ == target.texture.Get()) flush_pending();
    if (!context_ || !target.staging || image.pixels.empty() ||
        (image.format != ImageFormat::Bgra32 && image.format != ImageFormat::Bgr24) ||
        image.pitch < static_cast<std::uint64_t>(image.width) * bytes_per_pixel(image.format) ||
        static_cast<std::uint64_t>(image.pitch) * image.height > image.pixels.size()) return E_INVALIDARG;
    const RECT dirty = target.gpu_dirty;
    if (dirty.left >= dirty.right || dirty.top >= dirty.bottom ||
        dirty.right > static_cast<LONG>(image.width) || dirty.bottom > static_cast<LONG>(image.height)) {
        target.cpu_needs_readback = false;
        target.gpu_dirty = {};
        return S_OK;
    }
    const long long started = performance_counter();
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    bound_target_ = nullptr;
    compositor_.invalidate_renderer_bindings();
    ID3D11ShaderResourceView* empty[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, 5, empty);
    const D3D11_BOX region{static_cast<UINT>(dirty.left), static_cast<UINT>(dirty.top), 0,
        static_cast<UINT>(dirty.right), static_cast<UINT>(dirty.bottom), 1};
    context_->CopySubresourceRegion(target.staging.Get(), 0, region.left, region.top, 0, target.texture.Get(), 0, &region);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = context_->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;
    if (mapped.RowPitch < static_cast<std::uint64_t>(image.width) * 4) {
        context_->Unmap(target.staging.Get(), 0);
        return E_INVALIDARG;
    }
    const auto first = static_cast<std::size_t>(dirty.left), columns = static_cast<std::size_t>(dirty.right - dirty.left);
    for (auto y = static_cast<std::uint32_t>(dirty.top); y < static_cast<std::uint32_t>(dirty.bottom); ++y) {
        const auto* output_row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        auto* target_row = image.pixels.data() + static_cast<std::size_t>(y) * image.pitch;
        if (image.format == ImageFormat::Bgra32) {
            std::memcpy(target_row + first * 4, output_row + first * 4, columns * 4);
        } else {
            for (std::size_t x = first; x < first + columns; ++x) {
                std::memcpy(target_row + x * 3, output_row + x * 4, 3);
            }
        }
    }
    context_->Unmap(target.staging.Get(), 0);
    ++counters_.readbacks;
    counters_.transfer_bytes += static_cast<unsigned long long>(columns) * 4 * static_cast<unsigned long long>(dirty.bottom - dirty.top);
    target.gpu_dirty = {};
    counters_.readback_ticks += performance_counter() - started;
    target.cpu_needs_upload = false;
    target.cpu_needs_readback = false;
    return S_OK;
}

bool SurfaceRenderer::gpu_holds_newer_pixels(void* id) const noexcept {
    const auto found = surfaces_.find(id);
    return found != surfaces_.end() && found->second.cpu_needs_readback && !found->second.cpu_needs_upload;
}

HRESULT SurfaceRenderer::synchronize_region_to_cpu(void* id, DecodedImage& image, RECT region) noexcept {
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end()) return S_OK;
    auto& target = found->second;
    if (!target.cpu_needs_readback || target.cpu_needs_upload) return S_OK;
    if (pending_target_ == target.texture.Get()) flush_pending();
    if (!context_ || !target.staging || image.format != ImageFormat::Bgra32 || image.pixels.empty() ||
        image.pitch < static_cast<std::uint64_t>(image.width) * 4 ||
        static_cast<std::uint64_t>(image.pitch) * image.height > image.pixels.size()) return E_INVALIDARG;
    const RECT& dirty = target.gpu_dirty;
    region = {std::max({region.left, dirty.left, 0L}), std::max({region.top, dirty.top, 0L}),
        std::min({region.right, dirty.right, static_cast<LONG>(image.width)}),
        std::min({region.bottom, dirty.bottom, static_cast<LONG>(image.height)})};
    if (region.left >= region.right || region.top >= region.bottom) return S_OK;
    const long long started = performance_counter();
    const D3D11_BOX box{static_cast<UINT>(region.left), static_cast<UINT>(region.top), 0,
        static_cast<UINT>(region.right), static_cast<UINT>(region.bottom), 1};
    context_->CopySubresourceRegion(target.staging.Get(), 0, box.left, box.top, 0, target.texture.Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = context_->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;
    const auto columns = static_cast<std::size_t>(region.right - region.left) * 4;
    for (LONG y = region.top; y < region.bottom; ++y) {
        std::memcpy(image.pixels.data() + static_cast<std::size_t>(y) * image.pitch + static_cast<std::size_t>(region.left) * 4,
            static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch +
                static_cast<std::size_t>(region.left) * 4, columns);
    }
    context_->Unmap(target.staging.Get(), 0);
    ++counters_.readbacks;
    counters_.transfer_bytes += static_cast<unsigned long long>(columns) * static_cast<unsigned long long>(region.bottom - region.top);
    counters_.readback_ticks += performance_counter() - started;
    return S_OK;
}

HRESULT SurfaceRenderer::upload_region(void* id, const DecodedImage& image, RECT region) noexcept {
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end() || found->second.cpu_needs_upload) return S_OK;
    flush_pending();
    if (!context_ || image.format != ImageFormat::Bgra32 || image.pixels.empty() ||
        static_cast<std::uint64_t>(image.pitch) * image.height > image.pixels.size()) return E_INVALIDARG;
    region = {std::max(region.left, 0L), std::max(region.top, 0L), std::min(region.right, static_cast<LONG>(image.width)),
        std::min(region.bottom, static_cast<LONG>(image.height))};
    if (region.left >= region.right || region.top >= region.bottom) return S_OK;
    const long long started = performance_counter();
    const D3D11_BOX box{static_cast<UINT>(region.left), static_cast<UINT>(region.top), 0,
        static_cast<UINT>(region.right), static_cast<UINT>(region.bottom), 1};
    context_->UpdateSubresource(found->second.texture.Get(), 0, &box,
        image.pixels.data() + static_cast<std::size_t>(region.top) * image.pitch + static_cast<std::size_t>(region.left) * 4,
        image.pitch, 0);
    ++counters_.uploads;
    counters_.transfer_bytes += static_cast<unsigned long long>(region.right - region.left) * 4 *
        static_cast<unsigned long long>(region.bottom - region.top);
    counters_.upload_ticks += performance_counter() - started;
    return S_OK;
}

void SurfaceRenderer::mark_cpu_modified(void* id) noexcept {
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end()) return;
    found->second.cpu_needs_upload = true;
    found->second.cpu_needs_readback = false;
    found->second.gpu_dirty = {};
}

void SurfaceRenderer::mark_palette_modified(void* id) noexcept {
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end()) return;
    found->second.palette_needs_upload = true;
}

void SurfaceRenderer::forget(void* id) noexcept {
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end()) return;
    // Queued draws reference the views of this surface without holding them.
    flush_pending();
    if (context_ && bound_target_ == found->second.texture.Get()) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        bound_target_ = nullptr;
        compositor_.invalidate_renderer_bindings();
    }
    surfaces_.erase(found);
}

HRESULT SurfaceRenderer::open_screen(HWND window, const std::filesystem::path& config, std::string& error) {
    HRESULT hr = initialize();
    if (FAILED(hr)) { error = "Cannot initialize D3D11 device"; return hr; }
    return presentation_.initialize_configured(device_.Get(), window, config, error);
}

HRESULT SurfaceRenderer::set_screen_size(DisplaySize size) noexcept {
    return presentation_.set_render_size(size.width, size.height);
}

void SurfaceRenderer::close_screen() noexcept { presentation_.close(); }

void SurfaceRenderer::shutdown() noexcept {
    pending_.clear();
    pending_target_ = nullptr;
    if (context_) context_->ClearState();
    bound_target_ = nullptr;
    compositor_.invalidate_renderer_bindings();
    presentation_.close();
    surfaces_.clear();
    compositor_ = {};
    context_.Reset();
    device_.Reset();
}

DisplaySize SurfaceRenderer::screen_size() const noexcept {
    return presentation_.render_size();
}

DisplaySize SurfaceRenderer::output_size() const noexcept {
    return presentation_.output_size();
}

DisplayMode SurfaceRenderer::display_mode() const noexcept {
    return presentation_.display_mode();
}

std::optional<unsigned> SurfaceRenderer::max_fps() const noexcept {
    return presentation_.max_fps();
}

HRESULT SurfaceRenderer::present(void* id, const DecodedImage& image) noexcept {
    if (!id || image.format != ImageFormat::Bgra32 || image.pixels.empty() || !presentation_.back_buffer()) return E_INVALIDARG;
    Surface* source = nullptr;
    HRESULT hr = surface(id, image, source);
    if (FAILED(hr)) return hr;
    flush_pending();
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    bound_target_ = nullptr;
    compositor_.invalidate_renderer_bindings();
    ID3D11ShaderResourceView* empty[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, 5, empty);
    hr = upload_cpu_image(*source, image);
    if (FAILED(hr)) return hr;
    hr = presentation_.draw(context_.Get(), compositor_, source->view.Get(),
        {0, 0, static_cast<float>(image.width), static_cast<float>(image.height)},
        {0, 0, static_cast<float>(image.width), static_cast<float>(image.height)});
    if (FAILED(hr)) return hr;
    const FrameCounters counters = counters_;
    counters_ = {};
    return presentation_.present(counters);
}

}
