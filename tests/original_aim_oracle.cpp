#include "image_decoder.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc != 4 && argc != 5) {
        std::fputs("Usage: original_aim_oracle <game directory> <encoded image> <reference pixels> [reference palette]\n", stderr);
        return 2;
    }
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    const auto library_path = std::filesystem::path(argv[1]) / L"AIM.dll";
    sgl::ImageDecoder decoder;
    sgl::DecodedImage uninitialized;
    std::vector<std::uint8_t> empty;
    if (decoder.decode("Hauptmenu.jpg", empty, uninitialized) != E_INVALIDARG) return 3;
    if (FAILED(decoder.initialize(library_path))) return 4;
    std::ifstream input(argv[2], std::ios::binary);
    std::ifstream reference(argv[3], std::ios::binary);
    if (!input || !reference) return 6;
    std::vector<unsigned char> encoded((std::istreambuf_iterator<char>(input)), {});
    std::vector<unsigned char> expected((std::istreambuf_iterator<char>(reference)), {});
    std::vector<unsigned char> expected_palette;
    if (argc == 5) {
        std::ifstream palette(argv[4], std::ios::binary);
        if (!palette) return 6;
        expected_palette.assign(std::istreambuf_iterator<char>(palette), {});
    }
    if (encoded.empty() || expected.empty()) return 7;
    sgl::DecodedImage decoded;
    decoded.width = 42;
    if (decoder.decode("Hauptmenu.jpg", empty, decoded) != E_INVALIDARG) return 8;
    if (decoded.width != 42) return 8;
    const std::string name = std::filesystem::path(argv[2]).filename().string();
    const HMODULE aim = GetModuleHandleW(L"AIM.dll");
    const auto init = reinterpret_cast<void (__cdecl*)(void*)>(GetProcAddress(aim, "?AIM_INIT@@YAXPAUAIM_IMAGE@@@Z"));
    const auto info = reinterpret_cast<int (__cdecl*)(void*, const char*, unsigned char*, long)>(GetProcAddress(aim, "?AIM_GETFILEFORMAT_FROMMEM@@YAHPAUAIM_GFXDEF@@PBDPAEJ@Z"));
    const auto free_image = reinterpret_cast<void (__cdecl*)(void*)>(GetProcAddress(aim, "?AIM_FREE@@YAXPAUAIM_IMAGE@@@Z"));
    if (!init || !info || !free_image) return 13;
    std::array<std::uint32_t, 23> metadata = {};
    init(metadata.data());
    const int inspected = info(metadata.data(), name.c_str(), encoded.data(), static_cast<long>(encoded.size()));
    std::printf("AIM header: result=%d ptr=%08x palette=%08x width=%u height=%u bpp=%u format=%u pitch=%u\n",
        inspected, metadata[0], metadata[1], metadata[2], metadata[3], metadata[4], metadata[5], metadata[6]);
    const bool has_pixels = metadata[0] != 0;
    free_image(metadata.data());
    if (!inspected || has_pixels) return 14;
    sgl::DecodedImage header;
    if (FAILED(decoder.inspect(name.c_str(), encoded, header)) || !header.pixels.empty() || !header.palette.empty()) return 15;
    if (FAILED(decoder.decode(name.c_str(), encoded, decoded))) return 9;
    if (header.width != decoded.width || header.height != decoded.height || header.pitch != decoded.pitch || header.format != decoded.format) return 16;
    std::printf("AIM image: width=%u height=%u pitch=%u\n", decoded.width, decoded.height, decoded.pitch);
    if (!decoded.width || !decoded.height || decoded.pixels.size() != expected.size()) return 10;
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) if (decoded.pixels[i] != expected[i]) ++mismatches;
    std::printf("Reference bytes: %zu, mismatches: %zu\n", expected.size(), mismatches);
    if (mismatches || decoded.palette != expected_palette ||
        decoded.format != (argc == 5 ? sgl::ImageFormat::Indexed8 : sgl::ImageFormat::Bgra32)) return 11;
    if (FAILED(decoder.decode(name.c_str(), encoded, decoded)) || decoded.pixels != expected || decoded.palette != expected_palette) return 12;
    return 0;
}
