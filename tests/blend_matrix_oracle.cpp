// Draws every combination of source format, texture color flags, blend byte and modulation color with one SGL
// library and prints the destination pixels, so the original library and the replacement can be compared case by
// case. The replacement ends the process on an unsupported combination; run it again from the next case.
// AIM.dll and archiver.dll are loaded from the game directory first, as the game would have loaded them.
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

namespace {
struct LockedTexture {
    std::uint8_t* pixels;
    std::uint32_t* palette;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pitch;
};
static_assert(sizeof(LockedTexture) == 20);

template<typename T> T resolve(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

constexpr std::uint32_t key = 0x00ff00ffu;
constexpr std::array<std::uint32_t, 4> colors = {0xffffffffu, 0xff804020u, 0x80ffffffu, 0x00c0e060u};
constexpr int source_size = 8;
constexpr int target_size = 12;
constexpr int case_count = 2 * 8 * 8 * static_cast<int>(colors.size());

std::uint32_t source_pixel(int x, int y) {
    if ((x + y) % 5 == 0) return key;
    if ((x + 2 * y) % 7 == 0) return 0xffff00ffu;
    const auto alpha = static_cast<std::uint32_t>((x * 37 + y * 53) & 0xff);
    return (alpha << 24) | ((x * 29u + 40u) & 0xff) << 16 | ((y * 31u + 20u) & 0xff) << 8 | ((x * 3u + y * 17u) & 0xff);
}

std::uint32_t target_pixel(int x, int y) {
    return 0xff000000u | ((x * 19u + 60u) & 0xff) << 16 | ((y * 23u + 90u) & 0xff) << 8 | ((x * 7u + y * 11u + 30u) & 0xff);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::fputs("Usage: blend_matrix_oracle <SGL DLL> <game directory> [first case] [--original]\n", stderr);
        return 2;
    }
    const int first = argc >= 4 ? std::wcstol(argv[3], nullptr, 10) : 0;
    const bool original = argc >= 5 && std::wcscmp(argv[4], L"--original") == 0;
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    const std::wstring game(argv[2]);
    SetDllDirectoryW(game.c_str());
    if (!LoadLibraryW((game + L"\\AIM.dll").c_str()) || !LoadLibraryW((game + L"\\archiver.dll").c_str())) {
        std::fprintf(stderr, "Cannot load AIM.dll or archiver.dll from %ls\n", game.c_str());
        return 3;
    }
    std::array<wchar_t, MAX_PATH> full{};
    if (!GetFullPathNameW(argv[1], static_cast<DWORD>(full.size()), full.data(), nullptr)) return 3;
    std::wstring path(full.data());
    SetCurrentDirectoryW(game.c_str());
    const HMODULE library = LoadLibraryW(path.c_str());
    if (!library) {
        std::fprintf(stderr, "Cannot load %ls: %lu\n", path.c_str(), GetLastError());
        return 3;
    }
    const auto initialize = resolve<int (__cdecl*)()>(library, "sgl_InitHardware");
    const auto set_key = resolve<std::uint32_t (__cdecl*)(std::uint32_t)>(library, "sgl_SetColorKey");
    const auto create = resolve<void* (__cdecl*)(std::uint16_t, std::uint16_t, std::uint32_t)>(library, "sgl_CreateMemoryTexture");
    const auto free_texture = resolve<void (__cdecl*)(void*)>(library, "sgl_FreeMemoryTexture");
    const auto lock = resolve<int (__cdecl*)(void*, LockedTexture*, int)>(library, "sgl_LockTexture");
    const auto unlock = resolve<void (__cdecl*)(void*)>(library, "sgl_UnlockTexture");
    const auto change_palette = resolve<int (__cdecl*)(void*, std::uint32_t*)>(library, "sgl_ChangePalette");
    const auto set_source = resolve<void* (__cdecl*)(void*)>(library, "sgl_SetRenderSource");
    const auto set_target = resolve<void* (__cdecl*)(void*)>(library, "sgl_SetRenderDest");
    const auto clip_mode = resolve<int (__cdecl*)(int)>(library, "sgl_Set2dClippingMode");
    const auto clip_rect = resolve<void (__cdecl*)(int, int, int, int)>(library, "sgl_SetClipRect_4i");
    const auto set_color = resolve<std::uint32_t (__cdecl*)(std::uint32_t)>(library, "sgl_SetConstantColor");
    const auto get_color = resolve<std::uint32_t (__cdecl*)()>(library, "sgl_GetConstantColor");
    const auto draw = resolve<void (__cdecl*)(int, int, int, int, int, int)>(library, "sgl_DrawBitmapRect");
    if (!initialize || !set_key || !create || !free_texture || !lock || !unlock || !change_palette || !set_source ||
        !set_target || !clip_mode || !clip_rect || !set_color || !get_color || !draw) return 4;
    set_key(key);
    if (!initialize()) return 5;
    set_key(key);

    std::array<std::uint32_t, 256> palette{};
    for (int index = 0; index < 256; ++index) palette[index] = source_pixel(index % source_size, index / source_size % source_size);
    palette[0] = key;
    palette[1] = 0xffff00ffu;

    for (int number = first; number < case_count; ++number) {
        int rest = number;
        const auto color = colors[rest % colors.size()];
        rest /= static_cast<int>(colors.size());
        const auto blend = static_cast<std::uint8_t>(rest % 8);
        rest /= 8;
        const auto color_flags = static_cast<std::uint32_t>(rest % 8);
        const bool indexed = rest / 8 != 0;
        const std::uint32_t flags = color_flags | (indexed ? 0x20u : 0u);

        void* source = create(source_size, source_size, flags);
        void* target = create(target_size, target_size, 0);
        if (!source || !target) return 6;
        LockedTexture pixels{};
        if (!lock(source, &pixels, 2)) return 7;
        for (int y = 0; y < source_size; ++y) {
            for (int x = 0; x < source_size; ++x) {
                if (indexed) pixels.pixels[y * pixels.pitch + x] = static_cast<std::uint8_t>((x * 7 + y * 3) % 12);
                else {
                    const auto value = source_pixel(x, y);
                    std::memcpy(pixels.pixels + y * pixels.pitch + x * 4, &value, 4);
                }
            }
        }
        unlock(source);
        if (indexed) {
            auto copy = palette;
            change_palette(source, copy.data());
        }
        if (!lock(target, &pixels, 2)) return 8;
        for (int y = 0; y < target_size; ++y) {
            for (int x = 0; x < target_size; ++x) {
                const auto value = target_pixel(x, y);
                std::memcpy(pixels.pixels + y * pixels.pitch + x * 4, &value, 4);
            }
        }
        unlock(target);
        *(static_cast<std::uint8_t*>(source) + 0x37) = blend;
        set_color(color);
        if (original) {
            // The original library keeps the modulation color in two globals; SetConstantColor alone does not reach
            // the copy its blitters read.
            *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(library) + 0x6e93c) = color;
            *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(library) + 0x8d080) = color;
        }
        set_source(source);
        set_target(target);
        clip_mode(1);
        clip_rect(0, 0, target_size, target_size);
        draw(0, 0, 2, 2, source_size, source_size);
        set_source(nullptr);
        set_target(nullptr);
        if (!lock(target, &pixels, 1)) return 9;
        std::printf("%d %s flags=%u blend=%u color=%08x", number, indexed ? "indexed" : "bgra", color_flags, blend, color);
        for (int y = 0; y < target_size; ++y) {
            for (int x = 0; x < target_size; ++x) {
                std::uint32_t value = 0;
                std::memcpy(&value, pixels.pixels + y * pixels.pitch + x * 4, 4);
                std::printf(" %08x", value);
            }
        }
        std::printf("\n");
        std::fflush(stdout);
        unlock(target);
        free_texture(source);
        free_texture(target);
    }
    return 0;
}
