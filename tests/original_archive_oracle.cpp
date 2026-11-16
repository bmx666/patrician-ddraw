#include "archive_reader.hpp"
#include "compositor.hpp"
#include "image_decoder.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <climits>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    const bool inspect_archive = argc >= 4 && std::wcscmp(argv[2], L"--inspect") == 0;
    if ((!inspect_archive && argc != 3 && argc != 4) || (inspect_archive && argc < 4)) {
        std::fputs("Usage: original_archive_oracle <game directory> <expected image> [reference pixels]\n"
                   "       original_archive_oracle <game directory> --inspect <resource> [archive ...]\n", stderr);
        return 2;
    }
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    if (!SetCurrentDirectoryW(argv[1])) return 3;
    const HMODULE library = LoadLibraryW((std::filesystem::path(argv[1]) / L"archiver.dll").c_str());
    if (!library) return 4;
    const auto create = reinterpret_cast<void* (__cdecl*)(int)>(GetProcAddress(library, "arc_CreateContainer"));
    const auto append = reinterpret_cast<int (__cdecl*)(void*, const char*)>(GetProcAddress(library, "arc_AppendArchive"));
    const auto release = reinterpret_cast<void (__cdecl*)(void*)>(GetProcAddress(library, "arc_ReleaseContainer"));
    const auto open_file = reinterpret_cast<void* (__cdecl*)(void*, const char*)>(GetProcAddress(library, "arc_CreateFile"));
    const auto length = reinterpret_cast<int (__cdecl*)(void*)>(GetProcAddress(library, "arc_FileLength"));
    const auto read_file = reinterpret_cast<int (__cdecl*)(void*, void*, int)>(GetProcAddress(library, "arc_Read"));
    const auto close_file = reinterpret_cast<void (__cdecl*)(void*)>(GetProcAddress(library, "arc_ReleaseFile"));
    if (!create || !append || !release || !open_file || !length || !read_file || !close_file) return 5;
    sgl::ArchiveReader reader;
    if (FAILED(reader.initialize(std::filesystem::path(argv[1]) / L"archiver.dll"))) return 5;
    if (inspect_archive) {
        const std::string name = std::filesystem::path(argv[3]).string();
        void* container = create(0);
        if (!container) return 8;
        if (argc == 4) {
            if (!append(container, "p2arch0_eng.cpr")) {
                release(container);
                return 9;
            }
        } else {
            for (int index = 4; index < argc; ++index) {
                const std::string archive_name = std::filesystem::path(argv[index]).string();
                if (!append(container, archive_name.c_str())) {
                    std::fprintf(stderr, "Could not append archive: %s\n", archive_name.c_str());
                    release(container);
                    return 9;
                }
            }
        }
        std::vector<std::uint8_t> encoded;
        if (FAILED(reader.read(container, name.c_str(), encoded))) {
            release(container);
            return 10;
        }
        const HMODULE aim = LoadLibraryW((std::filesystem::path(argv[1]) / L"AIM.dll").c_str());
        if (!aim) {
            release(container);
            return 11;
        }
        const auto initialize = reinterpret_cast<void (__cdecl*)(void*)>(GetProcAddress(aim, "?AIM_INIT@@YAXPAUAIM_IMAGE@@@Z"));
        const auto decode = reinterpret_cast<int (__cdecl*)(void*, const char*, unsigned char*, long)>(GetProcAddress(aim, "?AIM_CONVERT_MEMFILE@@YAHPAUAIM_IMAGE@@PBDPAEJ@Z"));
        const auto convert_raw = reinterpret_cast<int (__cdecl*)(void*)>(GetProcAddress(aim, "?AIM_CONVERT_RAW@@YAHPAUAIM_IMAGE@@@Z"));
        const auto inspect = reinterpret_cast<int (__cdecl*)(void*, const char*, unsigned char*, long)>(GetProcAddress(aim, "?AIM_GETFILEFORMAT_FROMMEM@@YAHPAUAIM_GFXDEF@@PBDPAEJ@Z"));
        const auto free_image = reinterpret_cast<void (__cdecl*)(void*)>(GetProcAddress(aim, "?AIM_FREE@@YAXPAUAIM_IMAGE@@@Z"));
        if (!initialize || !decode || !convert_raw || !inspect || !free_image || encoded.size() > LONG_MAX) {
            FreeLibrary(aim);
            release(container);
            return 12;
        }
        std::array<std::uint32_t, 23> metadata = {};
        initialize(metadata.data());
        const int result = inspect(metadata.data(), name.c_str(), encoded.data(), static_cast<long>(encoded.size()));
        std::printf("AIM resource: %s bytes=%zu result=%d pixels=%08x palette=%08x width=%u height=%u bytes_per_pixel=%u format=%u pitch=%u\n",
            name.c_str(), encoded.size(), result, metadata[0], metadata[1], metadata[2], metadata[3], metadata[4], metadata[5], metadata[6]);
        free_image(metadata.data());
        metadata.fill(0);
        initialize(metadata.data());
        const int decode_result = decode(metadata.data(), name.c_str(), encoded.data(), static_cast<long>(encoded.size()));
        std::printf("AIM decode: result=%d pixels=%08x palette=%08x width=%u height=%u bytes_per_pixel=%u format=%u pitch=%u\n",
            decode_result, metadata[0], metadata[1], metadata[2], metadata[3], metadata[4], metadata[5], metadata[6]);
        const int raw_result = convert_raw(metadata.data());
        std::printf("AIM raw: result=%d pixels=%08x palette=%08x width=%u height=%u bytes_per_pixel=%u format=%u pitch=%u\n",
            raw_result, metadata[0], metadata[1], metadata[2], metadata[3], metadata[4], metadata[5], metadata[6]);
        free_image(metadata.data());
        sgl::ImageDecoder decoder;
        const HRESULT initialized = decoder.initialize(std::filesystem::path(argv[1]) / L"AIM.dll");
        sgl::DecodedImage decoded;
        const HRESULT status = SUCCEEDED(initialized) ? decoder.inspect(name.c_str(), encoded, decoded) : initialized;
        std::printf("Renderer decoder inspect: hr=0x%08lx width=%u height=%u pitch=%u\n",
            static_cast<unsigned long>(status), decoded.width, decoded.height, decoded.pitch);
        FreeLibrary(aim);
        release(container);
        return result ? 0 : 13;
    }
    std::ifstream input(argv[2], std::ios::binary);
    if (!input) return 6;
    const std::vector<std::uint8_t> expected((std::istreambuf_iterator<char>(input)), {});
    if (expected.empty()) return 7;
    void* container = create(0);
    if (!container) return 8;
    if (!append(container, "p2arch0_eng.cpr")) return 9;
    void* stream = open_file(container, "images/options/Hauptmenu.jpg");
    if (!stream) return 24;
    const int stream_size = length(stream);
    if (stream_size < 0 || static_cast<std::size_t>(stream_size) != expected.size()) return 25;
    std::vector<std::uint8_t> streamed(expected.size());
    const int stream_read = read_file(stream, streamed.data(), stream_size);
    close_file(stream);
    if (stream_read != stream_size || streamed != expected) return 26;
    std::printf("Archive stream: %d bytes, equal=yes\n", stream_read);
    sgl::ImageDecoder decoder;
    if (FAILED(decoder.initialize(std::filesystem::path(argv[1]) / L"AIM.dll"))) return 13;
    std::vector<std::uint8_t> bytes = {42};
    if (reader.read(container, "", bytes) != E_INVALIDARG || bytes != std::vector<std::uint8_t>{42}) return 10;
    if (reader.read(container, "__missing_p3_test_resource__", bytes) != E_FAIL || bytes != std::vector<std::uint8_t>{42}) return 10;
    if (FAILED(reader.read(container, "images/options/Hauptmenu.jpg", bytes))) return 10;
    const bool matches = bytes == expected;
    std::printf("Archive image: %zu bytes, equal=%s\n", bytes.size(), matches ? "yes" : "no");
    if (!matches) return 11;

    constexpr const char* font_images[] = {
        "fonts/tiepolo_black16.aim", "fonts/tiepolo_bold16.aim", "fonts/tiepolo_black20.aim",
        "fonts/elgreco24.aim", "fonts/tiepolo_bold24.aim", "fonts/elgreco72.aim"
    };
    for (const char* name : font_images) {
        std::vector<std::uint8_t> font_bytes;
        if (FAILED(reader.read(container, name, font_bytes))) return 27;
        sgl::DecodedImage font;
        if (FAILED(decoder.inspect(name, font_bytes, font)) || !font.width || !font.height ||
            !font.pixels.empty() || !font.palette.empty()) return 28;
        std::printf("Archive font: %s %zu bytes %ux%u\n", name, font_bytes.size(), font.width, font.height);
    }
    release(container);
    if (argc == 3) return 0;
    std::ifstream reference_file(argv[3], std::ios::binary);
    if (!reference_file) return 12;
    const std::vector<std::uint8_t> reference((std::istreambuf_iterator<char>(reference_file)), {});
    sgl::DecodedImage image;
    if (FAILED(decoder.decode("images/options/Hauptmenu.jpg", bytes, image)) ||
        image.format != sgl::ImageFormat::Bgra32 || image.width != 800 || image.height != 600 ||
        image.pitch != 3200 || image.pixels != reference) return 14;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context))) return 15;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = image.width;
    desc.Height = image.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA input_data = {image.pixels.data(), image.pitch, 0};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> input_texture;
    if (FAILED(device->CreateTexture2D(&desc, &input_data, &input_texture))) return 16;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source;
    if (FAILED(device->CreateShaderResourceView(input_texture.Get(), nullptr, &source))) return 17;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &output))) return 18;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(device->CreateRenderTargetView(output.Get(), nullptr, &target))) return 19;
    sgl::Compositor compositor;
    if (FAILED(compositor.initialize(device.Get())) ||
        FAILED(compositor.copy(context.Get(), target.Get(), source.Get(), image.width, image.height,
            {0, 0, 800, 600}, {0, 0, 800, 600}, {0, 0, 800, 600}))) return 20;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return 21;
    context->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 22;
    bool pixels_match = true;
    for (std::size_t y = 0; y < image.height; ++y) {
        const auto gpu_row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        if (std::memcmp(gpu_row, reference.data() + y * image.pitch, image.pitch) != 0) pixels_match = false;
    }
    context->Unmap(staging.Get(), 0);
    std::printf("Archive -> AIM -> D3D11: %s\n", pixels_match ? "all menu pixels match" : "pixel mismatch");
    return pixels_match ? 0 : 23;
}
