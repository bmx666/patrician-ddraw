#include "compositor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using Microsoft::WRL::ComPtr;

int main(int argc, char** argv) {
    if (argc < 5 || argc > 7) {
        std::fputs("Usage: captured_texture_test <pixel file> <width> <height> <pitch> [BGRA palette file [color key]]\n", stderr);
        return 2;
    }
    const bool indexed = argc >= 6;
    char* end = nullptr;
    const unsigned long width = std::strtoul(argv[2], &end, 10);
    if (*end != '\0' || width < 64 || width > 16384) return 3;
    const unsigned long height = std::strtoul(argv[3], &end, 10);
    if (*end != '\0' || height < 8 || height > 16384) return 3;
    const unsigned long pitch = std::strtoul(argv[4], &end, 10);
    if (*end != '\0' || pitch < width * (indexed ? 1 : 4) || pitch > 65536) return 3;
    std::optional<std::uint32_t> color_key;
    if (argc == 7) {
        const unsigned long value = std::strtoul(argv[6], &end, 0);
        if (end == argv[6] || *end != '\0') return 3;
        color_key = value;
    }
    const auto size = static_cast<std::size_t>(pitch) * height;
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != static_cast<std::streamoff>(size)) return 4;
    file.seekg(0);
    std::vector<std::uint8_t> pixels(size);
    if (!file.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(size))) return 4;
    std::vector<std::uint8_t> palette_data;
    if (indexed) {
        std::ifstream palette_file(argv[5], std::ios::binary | std::ios::ate);
        if (!palette_file || palette_file.tellg() != 1024) return 4;
        palette_file.seekg(0);
        palette_data.resize(1024);
        if (!palette_file.read(reinterpret_cast<char*>(palette_data.data()), 1024)) return 4;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(result)) return 5;
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = indexed ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA input = {pixels.data(), pitch, 0};
    ComPtr<ID3D11Texture2D> texture;
    result = device->CreateTexture2D(&description, &input, &texture);
    if (FAILED(result)) return 6;
    ComPtr<ID3D11ShaderResourceView> source;
    result = device->CreateShaderResourceView(texture.Get(), nullptr, &source);
    if (FAILED(result)) return 7;
    ComPtr<ID3D11Texture2D> palette_texture;
    ComPtr<ID3D11ShaderResourceView> palette;
    if (indexed) {
        D3D11_TEXTURE2D_DESC palette_desc = description;
        palette_desc.Width = 256;
        palette_desc.Height = 1;
        palette_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        const D3D11_SUBRESOURCE_DATA palette_input = {palette_data.data(), 1024, 0};
        if (FAILED(device->CreateTexture2D(&palette_desc, &palette_input, &palette_texture))) return 16;
        if (FAILED(device->CreateShaderResourceView(palette_texture.Get(), nullptr, &palette))) return 17;
    }
    description.Width = 24;
    description.Height = 8;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> output;
    result = device->CreateTexture2D(&description, nullptr, &output);
    if (FAILED(result)) return 8;
    ComPtr<ID3D11RenderTargetView> target;
    result = device->CreateRenderTargetView(output.Get(), nullptr, &target);
    if (FAILED(result)) return 9;
    sgl::Compositor compositor;
    if (FAILED(compositor.initialize(device.Get()))) return 10;
    const float clear[] = {0, 0, 0, 0};
    context->ClearRenderTargetView(target.Get(), clear);
    if (indexed) {
        if (FAILED(compositor.copy_indexed(context.Get(), target.Get(), source.Get(), palette.Get(), 24, 8,
            {48, 0, 56, 8}, {0, 0, 8, 8}, {0, 0, 24, 8}, color_key))) return 11;
        if (FAILED(compositor.copy_indexed(context.Get(), target.Get(), source.Get(), palette.Get(), 24, 8,
            {56, 0, 64, 8}, {16, 0, 24, 8}, {0, 0, 24, 8}, color_key))) return 12;
    } else {
        if (FAILED(compositor.copy(context.Get(), target.Get(), source.Get(), 24, 8,
            {48, 0, 56, 8}, {0, 0, 8, 8}, {0, 0, 24, 8}))) return 11;
        if (FAILED(compositor.copy(context.Get(), target.Get(), source.Get(), 24, 8,
            {56, 0, 64, 8}, {16, 0, 24, 8}, {0, 0, 24, 8}))) return 12;
    }

    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &staging))) return 13;
    context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 14;
    bool matches = true;
    for (std::size_t y = 0; y < 8; ++y) {
        const auto row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        for (std::size_t x = 0; x < 24; ++x) {
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const std::size_t source_x = 48 + (x < 8 ? x : x - 8);
                std::uint8_t expected = 0;
                if (x < 8 || x >= 16) {
                    if (indexed) {
                        const std::size_t palette_offset = static_cast<std::size_t>(pixels[y * pitch + source_x]) * 4;
                        const std::uint32_t color = static_cast<std::uint32_t>(palette_data[palette_offset]) |
                            (static_cast<std::uint32_t>(palette_data[palette_offset + 1]) << 8) |
                            (static_cast<std::uint32_t>(palette_data[palette_offset + 2]) << 16) |
                            (static_cast<std::uint32_t>(palette_data[palette_offset + 3]) << 24);
                        if (!color_key || color != (*color_key & 0xffffff)) expected = palette_data[palette_offset + channel];
                    } else expected = pixels[y * pitch + source_x * 4 + channel];
                }
                if (row[x * 4 + channel] != expected) matches = false;
            }
        }
    }
    context->Unmap(staging.Get(), 0);
    if (!matches) {
        std::fputs("Captured texture pixels did not match the GPU result.\n", stderr);
        return 15;
    }
    std::puts(indexed ? "Two captured indexed texture regions matched the GPU palette result byte-for-byte." :
        "Two captured BGRA texture regions matched the GPU result byte-for-byte.");
    return 0;
}
