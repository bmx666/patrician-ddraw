#include "surface_renderer.hpp"
#include <climits>
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

HRESULT SurfaceRenderer::upload_cpu_image(Surface& surface, const DecodedImage& image) noexcept {
    if (surface.cpu_needs_upload) {
        std::vector<std::uint8_t> converted;
        const std::uint8_t* pixels = nullptr;
        UINT pitch = 0;
        const HRESULT hr = as_bgra(image, converted, pixels, pitch);
        if (FAILED(hr)) return hr;
        context_->UpdateSubresource(surface.texture.Get(), 0, nullptr, pixels, pitch, 0);
        surface.cpu_needs_upload = false;
        surface.cpu_needs_readback = false;
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
        (blend != BlendMode::Opaque && key)) return E_INVALIDARG;
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
    context_->OMSetRenderTargets(0, nullptr, nullptr);
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
    if (blend != BlendMode::Opaque) {
        if (!dst->background) {
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
        const float target_width = static_cast<float>(target.width);
        const float target_height = static_cast<float>(target.height);
        const LONG draw_left = static_cast<LONG>(std::floor(std::clamp(target_rect.left, 0.0f, target_width)));
        const LONG draw_top = static_cast<LONG>(std::floor(std::clamp(target_rect.top, 0.0f, target_height)));
        const LONG draw_right = static_cast<LONG>(std::ceil(std::clamp(target_rect.right, 0.0f, target_width)));
        const LONG draw_bottom = static_cast<LONG>(std::ceil(std::clamp(target_rect.bottom, 0.0f, target_height)));
        const LONG clip_left = std::clamp<LONG>(clip.left, 0, static_cast<LONG>(target.width));
        const LONG clip_top = std::clamp<LONG>(clip.top, 0, static_cast<LONG>(target.height));
        const LONG clip_right = std::clamp<LONG>(clip.right, 0, static_cast<LONG>(target.width));
        const LONG clip_bottom = std::clamp<LONG>(clip.bottom, 0, static_cast<LONG>(target.height));
        const LONG copy_left = std::max(draw_left, clip_left);
        const LONG copy_top = std::max(draw_top, clip_top);
        const LONG copy_right = std::min(draw_right, clip_right);
        const LONG copy_bottom = std::min(draw_bottom, clip_bottom);
        if (copy_left < copy_right && copy_top < copy_bottom) {
            const D3D11_BOX region = {static_cast<UINT>(copy_left), static_cast<UINT>(copy_top), 0,
                static_cast<UINT>(copy_right), static_cast<UINT>(copy_bottom), 1};
            context_->CopySubresourceRegion(dst->background.Get(), 0, static_cast<UINT>(copy_left),
                static_cast<UINT>(copy_top), 0, dst->texture.Get(), 0, &region);
        }
    }
    if (failure_stage) *failure_stage = "compositor draw";
    const Compositor::RendererSource renderer_source{source.width, source.height,
        source.format == ImageFormat::Indexed8 ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_B8G8R8A8_UNORM};
    if (mask) {
        hr = compositor_.copy_impl(context_.Get(), dst->target.Get(), src->view.Get(),
            src->palette_view.Get(), mask_surface->view.Get(), target.width, target.height,
            source_rect, target_rect, clip, std::nullopt, Sampling::SglFixedPoint,
            dst->background_view.Get(), modulation_color, BlendMode::AlphaMask, failure_stage, &renderer_source);
    } else if (source.format == ImageFormat::Indexed8) {
        hr = compositor_.copy_impl(context_.Get(), dst->target.Get(), src->view.Get(), src->palette_view.Get(), nullptr,
            target.width, target.height, source_rect, target_rect, clip, key, sampling,
            blend != BlendMode::Opaque ? dst->background_view.Get() : nullptr,
            modulation_color, blend, failure_stage, &renderer_source);
    } else {
        hr = compositor_.copy_impl(context_.Get(), dst->target.Get(), src->view.Get(), nullptr, nullptr,
            target.width, target.height, source_rect, target_rect, clip, key, sampling,
            blend != BlendMode::Opaque ? dst->background_view.Get() : nullptr,
            modulation_color, blend, failure_stage, &renderer_source);
    }
    if (FAILED(hr)) return hr;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    dst->cpu_needs_upload = false;
    dst->cpu_needs_readback = true;
    return S_OK;
}

HRESULT SurfaceRenderer::synchronize_to_cpu(void* id, DecodedImage& image) noexcept {
    if (!id) return E_INVALIDARG;
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end()) return S_OK;
    auto& target = found->second;
    if (!target.cpu_needs_readback || target.cpu_needs_upload) return S_OK;
    if (!context_ || !target.staging || image.pixels.empty() ||
        (image.format != ImageFormat::Bgra32 && image.format != ImageFormat::Bgr24) ||
        image.pitch < static_cast<std::uint64_t>(image.width) * bytes_per_pixel(image.format) ||
        static_cast<std::uint64_t>(image.pitch) * image.height > image.pixels.size()) return E_INVALIDARG;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* empty[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, 5, empty);
    context_->CopyResource(target.staging.Get(), target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = context_->Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;
    if (mapped.RowPitch < static_cast<std::uint64_t>(image.width) * 4) {
        context_->Unmap(target.staging.Get(), 0);
        return E_INVALIDARG;
    }
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const auto* output_row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
        auto* target_row = image.pixels.data() + static_cast<std::size_t>(y) * image.pitch;
        if (image.format == ImageFormat::Bgra32) {
            std::memcpy(target_row, output_row, static_cast<std::size_t>(image.width) * 4);
        } else {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                std::memcpy(target_row + static_cast<std::size_t>(x) * 3,
                    output_row + static_cast<std::size_t>(x) * 4, 3);
            }
        }
    }
    context_->Unmap(target.staging.Get(), 0);
    target.cpu_needs_upload = false;
    target.cpu_needs_readback = false;
    return S_OK;
}

void SurfaceRenderer::mark_cpu_modified(void* id) noexcept {
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end()) return;
    found->second.cpu_needs_upload = true;
    found->second.cpu_needs_readback = false;
}

void SurfaceRenderer::mark_palette_modified(void* id) noexcept {
    const auto found = surfaces_.find(id);
    if (found == surfaces_.end()) return;
    found->second.palette_needs_upload = true;
}

void SurfaceRenderer::forget(void* id) noexcept { surfaces_.erase(id); }

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
    if (context_) context_->ClearState();
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

HRESULT SurfaceRenderer::present(void* id, const DecodedImage& image) noexcept {
    if (!id || image.format != ImageFormat::Bgra32 || image.pixels.empty() || !presentation_.back_buffer()) return E_INVALIDARG;
    Surface* source = nullptr;
    HRESULT hr = surface(id, image, source);
    if (FAILED(hr)) return hr;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* empty[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    context_->PSSetShaderResources(0, 5, empty);
    hr = upload_cpu_image(*source, image);
    if (FAILED(hr)) return hr;
    hr = presentation_.draw(context_.Get(), compositor_, source->view.Get(),
        {0, 0, static_cast<float>(image.width), static_cast<float>(image.height)},
        {0, 0, static_cast<float>(image.width), static_cast<float>(image.height)});
    if (FAILED(hr)) return hr;
    return presentation_.present();
}

}
