#include "compositor.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#ifdef P3_RENDERDOC_CAPTURE
#include "renderdoc_app.h"
#endif

using Microsoft::WRL::ComPtr;

int main(int argc, char** argv) {
#ifdef P3_RENDERDOC_CAPTURE
    if (argc != 3) return 20;
    const HMODULE renderdoc = LoadLibraryA(argv[1]);
    if (!renderdoc) return 21;
    const auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(renderdoc, "RENDERDOC_GetAPI"));
    RENDERDOC_API_1_6_0* api = nullptr;
    if (!get_api || get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&api)) != 1) return 22;
    api->SetCaptureFilePathTemplate(argv[2]);
#else
    (void)argc;
    (void)argv;
#endif
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(result)) return 1;
#ifdef P3_RENDERDOC_CAPTURE
    api->StartFrameCapture(device.Get(), nullptr);
    if (!api->IsFrameCapturing()) return 23;
#endif

    const std::array<std::uint32_t, 4> pixels = {
        0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff,
    };
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = 2;
    description.Height = 2;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data = {pixels.data(), 2 * sizeof(pixels[0]), 0};
    ComPtr<ID3D11Texture2D> texture;
    result = device->CreateTexture2D(&description, &data, &texture);
    if (FAILED(result)) return 2;
    ComPtr<ID3D11ShaderResourceView> source;
    result = device->CreateShaderResourceView(texture.Get(), nullptr, &source);
    if (FAILED(result)) return 3;
    const std::uint32_t transparent_red = 0x000000ff;
    D3D11_TEXTURE2D_DESC key_description = {};
    key_description.Width = key_description.Height = 1;
    key_description.MipLevels = key_description.ArraySize = 1;
    key_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    key_description.SampleDesc.Count = 1;
    key_description.Usage = D3D11_USAGE_IMMUTABLE;
    key_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA key_data = {&transparent_red, sizeof(transparent_red), 0};
    ComPtr<ID3D11Texture2D> key_texture;
    result = device->CreateTexture2D(&key_description, &key_data, &key_texture);
    if (FAILED(result)) return 47;
    ComPtr<ID3D11ShaderResourceView> key_source;
    result = device->CreateShaderResourceView(key_texture.Get(), nullptr, &key_source);
    if (FAILED(result)) return 48;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ComPtr<ID3D11Texture2D> bgra_texture;
    result = device->CreateTexture2D(&description, &data, &bgra_texture);
    if (FAILED(result)) return 17;
    ComPtr<ID3D11ShaderResourceView> bgra_source;
    result = device->CreateShaderResourceView(bgra_texture.Get(), nullptr, &bgra_source);
    if (FAILED(result)) return 18;
    const std::array<std::uint8_t, 4> indices = {0, 1, 2, 3};
    description.Format = DXGI_FORMAT_R8_UINT;
    const D3D11_SUBRESOURCE_DATA index_data = {indices.data(), 2, 0};
    ComPtr<ID3D11Texture2D> index_texture;
    result = device->CreateTexture2D(&description, &index_data, &index_texture);
    if (FAILED(result)) return 25;
    ComPtr<ID3D11ShaderResourceView> index_source;
    result = device->CreateShaderResourceView(index_texture.Get(), nullptr, &index_source);
    if (FAILED(result)) return 26;
    std::array<std::uint32_t, 256> palette_values = {};
    for (std::size_t index = 0; index < pixels.size(); ++index) palette_values[index] = pixels[index];
    palette_values[0] = 0x000000ff;
    palette_values[1] = 0xff0000ff;
    description.Width = 256;
    description.Height = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    const D3D11_SUBRESOURCE_DATA palette_data = {palette_values.data(), sizeof(palette_values), 0};
    ComPtr<ID3D11Texture2D> palette_texture;
    result = device->CreateTexture2D(&description, &palette_data, &palette_texture);
    if (FAILED(result)) return 27;
    ComPtr<ID3D11ShaderResourceView> palette;
    result = device->CreateShaderResourceView(palette_texture.Get(), nullptr, &palette);
    if (FAILED(result)) return 28;

    description.Width = 8;
    description.Height = 8;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> output;
    result = device->CreateTexture2D(&description, nullptr, &output);
    if (FAILED(result)) return 4;
    std::array<std::uint32_t, 64> background_pixels;
    background_pixels.fill(0xff000000);
    const D3D11_SUBRESOURCE_DATA background_data = {background_pixels.data(), 8 * sizeof(background_pixels[0]), 0};
    ComPtr<ID3D11Texture2D> background_texture;
    result = device->CreateTexture2D(&description, &background_data, &background_texture);
    if (FAILED(result)) return 49;
    ComPtr<ID3D11ShaderResourceView> background;
    result = device->CreateShaderResourceView(background_texture.Get(), nullptr, &background);
    if (FAILED(result)) return 50;
    ComPtr<ID3D11RenderTargetView> target;
    result = device->CreateRenderTargetView(output.Get(), nullptr, &target);
    if (FAILED(result)) return 5;

    sgl::Compositor compositor;
    result = compositor.initialize(device.Get());
    if (FAILED(result)) {
        std::fprintf(stderr, "Compositor initialization failed: 0x%08lx\n", static_cast<unsigned long>(result));
        return 6;
    }
    const float clear[] = {0, 0, 0, 1};
    context->ClearRenderTargetView(target.Get(), clear);
    const sgl::Rect full_source = {0, 0, 2, 2};
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        full_source, {0, 0, 4, 4}, {0, 0, 8, 8});
    if (FAILED(result)) return 7;
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        full_source, {4, 4, 8, 8}, {6, 4, 8, 8});
    if (FAILED(result)) return 8;
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        {1, 1, 2, 2}, {6, 0, 8, 2}, {0, 0, 8, 8});
    if (FAILED(result)) return 13;
    result = compositor.copy(context.Get(), target.Get(), bgra_source.Get(), 8, 8,
        {0, 0, 1, 1}, {4, 0, 5, 1}, {0, 0, 8, 8});
    if (FAILED(result)) return 19;
    result = compositor.copy_indexed(context.Get(), target.Get(), index_source.Get(), palette.Get(), 8, 8,
        full_source, {4, 2, 8, 4}, {0, 0, 8, 8});
    if (FAILED(result)) return 29;
    result = compositor.copy_indexed(context.Get(), target.Get(), index_source.Get(), palette.Get(), 8, 8,
        full_source, {0, 4, 4, 8}, {0, 0, 8, 8}, 0x00ff0000);
    if (FAILED(result)) return 33;
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        {0, 0, 1, 1}, {5, 0, 6, 1}, {0, 0, 8, 8}, sgl::Sampling::SglFixedPoint,
        std::nullopt, nullptr, nullptr, 0x8080ff40);
    if (FAILED(result)) return 41;
    result = compositor.copy_indexed(context.Get(), target.Get(), index_source.Get(), palette.Get(), 8, 8,
        {1, 0, 2, 1}, {5, 1, 6, 2}, {0, 0, 8, 8}, std::nullopt,
        sgl::Sampling::SglFixedPoint, nullptr, 0x8080ff40);
    if (FAILED(result)) return 42;
    result = compositor.copy_indexed(context.Get(), target.Get(), index_source.Get(), nullptr, 8, 8,
        full_source, {0, 0, 4, 4}, {0, 0, 8, 8});
    if (result != E_INVALIDARG) return 30;
    result = compositor.copy_indexed(context.Get(), target.Get(), source.Get(), palette.Get(), 8, 8,
        full_source, {0, 0, 4, 4}, {0, 0, 8, 8});
    if (result != E_NOTIMPL) return 31;
    result = compositor.copy_indexed(context.Get(), target.Get(), index_source.Get(), bgra_source.Get(), 8, 8,
        full_source, {0, 0, 4, 4}, {0, 0, 8, 8});
    if (result != E_INVALIDARG) return 32;
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        {0, 0, 3, 2}, {0, 0, 4, 4}, {0, 0, 8, 8});
    if (result != E_INVALIDARG) return 9;
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 9, 8,
        full_source, {0, 0, 4, 4}, {0, 0, 8, 8});
    if (result != E_INVALIDARG) return 14;
    ComPtr<ID3D11ShaderResourceView> same_target;
    result = device->CreateShaderResourceView(output.Get(), nullptr, &same_target);
    if (FAILED(result)) return 15;
    result = compositor.copy(context.Get(), target.Get(), same_target.Get(), 8, 8,
        {0, 0, 8, 8}, {0, 0, 8, 8}, {0, 0, 8, 8});
    if (result != E_INVALIDARG) return 16;
#ifdef P3_RENDERDOC_CAPTURE
    if (api->EndFrameCapture(device.Get(), nullptr) != 1 || api->GetNumCaptures() != 1) return 24;
#endif

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    result = device->CreateTexture2D(&description, nullptr, &staging);
    if (FAILED(result)) return 10;
    context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) return 11;
    bool matches = true;
    for (UINT y = 0; y < 8; ++y) {
        const auto row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < 8; ++x) {
            std::uint32_t expected = 0xff000000;
            if (x < 4 && y < 4) expected = pixels[(y / 2) * 2 + x / 2];
            if (x >= 6 && y >= 4) expected = pixels[((y - 4) / 2) * 2 + (x - 4) / 2];
            if (x >= 6 && y < 2) expected = pixels[3];
            if (x == 4 && y == 0) expected = pixels[2];
            if ((x == 5 && y == 0) || (x == 5 && y == 1)) expected = 0x80000080;
            if (x >= 4 && y >= 2 && y < 4) expected = palette_values[(y - 2) * 2 + (x - 4) / 2];
            if (x < 4 && y >= 4) {
                const auto index = ((y - 4) / 2) * 2 + x / 2;
                if (index != 0) expected = palette_values[index];
            }
            if (row[x] != expected) matches = false;
        }
    }
    context->Unmap(staging.Get(), 0);
    if (!matches) {
        std::fputs("GPU pixels did not match the expected rectangles and clip.\n", stderr);
        return 12;
    }
    context->ClearRenderTargetView(target.Get(), clear);
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        full_source, {1.25f, 1.5f, 7.25f, 7.5f}, {0, 0, 8, 8}, sgl::Sampling::SglFixedPoint);
    if (FAILED(result)) return 35;
    context->CopyResource(staging.Get(), output.Get());
    result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) return 36;
    matches = true;
    unsigned fractional_mismatches = 0;
    for (UINT y = 0; y < 8; ++y) {
        const auto row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < 8; ++x) {
            std::uint32_t expected = 0xff000000;
            if (x >= 1 && x <= 6 && y >= 1 && y <= 6) {
                const UINT source_x = x <= 3 ? 0 : 1;
                const UINT source_y = y <= 3 ? 0 : 1;
                expected = pixels[source_y * 2 + source_x];
            }
            if (row[x] != expected) {
                if (fractional_mismatches < 16)
                    std::fprintf(stderr, "Fractional (%u,%u): actual %08x expected %08x\n", x, y, row[x], expected);
                ++fractional_mismatches;
                matches = false;
            }
        }
    }
    context->Unmap(staging.Get(), 0);
    if (!matches) {
        std::fprintf(stderr, "Fractional transformed destination had %u mismatches.\n", fractional_mismatches);
        return 37;
    }
    context->ClearRenderTargetView(target.Get(), clear);
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        {0.5f, 0.5f, 2.0f, 2.0f}, {0, 0, 4, 4}, {0, 0, 8, 8}, sgl::Sampling::SglFixedPoint);
    if (FAILED(result)) return 38;
    context->CopyResource(staging.Get(), output.Get());
    result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) return 39;
    matches = true;
    for (UINT y = 0; y < 4; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < 4; ++x) {
            const auto source_x = x == 0 ? 0u : 1u;
            const auto source_y = y == 0 ? 0u : 1u;
            if (row[x] != pixels[source_y * 2 + source_x]) matches = false;
        }
    }
    context->Unmap(staging.Get(), 0);
    if (!matches) {
        std::fputs("Fractional source origin sampled the wrong texels.\n", stderr);
        return 40;
    }
    const float white[] = {1, 1, 1, 1};
    context->ClearRenderTargetView(target.Get(), white);
    result = compositor.copy(context.Get(), target.Get(), key_source.Get(), 8, 8,
        {0, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 8, 8}, sgl::Sampling::Center,
        0x00ff0000, nullptr, nullptr, 0xff000000);
    if (FAILED(result)) return 43;
    result = compositor.copy_indexed(context.Get(), target.Get(), index_source.Get(), palette.Get(), 8, 8,
        {0, 0, 1, 1}, {1, 0, 2, 1}, {0, 0, 8, 8}, 0x00ff0000,
        sgl::Sampling::Center, nullptr, 0xff000000);
    if (FAILED(result)) return 44;
    context->CopyResource(staging.Get(), output.Get());
    result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) return 45;
    const auto* keyed_row = reinterpret_cast<const std::uint32_t*>(mapped.pData);
    const auto keyed_pixel = keyed_row[0];
    const auto indexed_keyed_pixel = keyed_row[1];
    const bool key_before_modulation = keyed_pixel == 0xffffffff && indexed_keyed_pixel == 0xffffffff;
    context->Unmap(staging.Get(), 0);
    if (!key_before_modulation) {
        std::fputs("Color-keyed pixels were not discarded before modulation.\n", stderr);
        return 46;
    }
    context->ClearRenderTargetView(target.Get(), clear);
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        {0, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 8, 8}, sgl::Sampling::Center,
        std::nullopt, background.Get(), nullptr, 0xff000000, sgl::BlendMode::Alpha);
    if (FAILED(result)) return 51;
    context->CopyResource(staging.Get(), output.Get());
    result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) return 52;
    const auto alpha_modulated_pixel = *reinterpret_cast<const std::uint32_t*>(mapped.pData);
    context->Unmap(staging.Get(), 0);
    if ((alpha_modulated_pixel & 0x00ffffff) != 0) {
        std::fputs("Alpha-blended draw ignored its modulation color.\n", stderr);
        return 53;
    }
    context->ClearRenderTargetView(target.Get(), clear);
    result = compositor.copy(context.Get(), target.Get(), source.Get(), 8, 8,
        {0, 0, 1, 1}, {0, 0, 1, 1}, {0, 0, 8, 8}, sgl::Sampling::Center,
        std::nullopt, background.Get(), nullptr, 0xff804020, sgl::BlendMode::AdditiveModulated);
    if (FAILED(result)) return 54;
    result = compositor.copy_indexed(context.Get(), target.Get(), index_source.Get(), palette.Get(), 8, 8,
        {1, 1, 2, 2}, {1, 0, 2, 1}, {0, 0, 8, 8}, std::nullopt,
        sgl::Sampling::Center, nullptr, 0xff804020, background.Get(), sgl::BlendMode::AdditiveModulated);
    if (FAILED(result)) return 55;
    context->CopyResource(staging.Get(), output.Get());
    result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) return 56;
    const auto* modulated_pixels = static_cast<const std::uint8_t*>(mapped.pData);
    const std::array<std::uint8_t, 8> actual_additive_pixels = {
        modulated_pixels[0], modulated_pixels[1], modulated_pixels[2], modulated_pixels[3],
        modulated_pixels[4], modulated_pixels[5], modulated_pixels[6], modulated_pixels[7],
    };
    const bool additive_modulation_matches =
        actual_additive_pixels[0] == 127 && actual_additive_pixels[1] == 0 && actual_additive_pixels[2] == 0 && actual_additive_pixels[3] == 255 &&
        actual_additive_pixels[4] == 127 && actual_additive_pixels[5] == 63 && actual_additive_pixels[6] == 31 && actual_additive_pixels[7] == 255;
    context->Unmap(staging.Get(), 0);
    if (!additive_modulation_matches) {
        std::fprintf(stderr, "Tinted additive result was %u,%u,%u,%u / %u,%u,%u,%u; expected 127,0,0,255 / 127,63,31,255.\n",
            actual_additive_pixels[0], actual_additive_pixels[1], actual_additive_pixels[2], actual_additive_pixels[3],
            actual_additive_pixels[4], actual_additive_pixels[5], actual_additive_pixels[6], actual_additive_pixels[7]);
        return 57;
    }
    std::puts("RGBA, BGRA and indexed textures, color modulation, keying, alpha and tinted additive blending, fractional scaling, clipping and invalid input verified on D3D11.");
    return 0;
}
