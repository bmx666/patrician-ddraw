#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

struct LockOutput { std::uint8_t* pixels; void* palette; std::uint32_t width, height, pitch; };

template<typename T> T resolve(HMODULE library, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(library, name));
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || !SetDllDirectoryW(argv[1]) || !SetCurrentDirectoryW(argv[1])) return 1;
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    DEVMODEW before = {};
    before.dmSize = sizeof(before);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &before)) return 2;
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 800, 600,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 3;
    RECT initial_client = {};
    if (!GetClientRect(window, &initial_client)) return 9;
    const HMODULE library = LoadLibraryW((std::wstring(argv[1]) + L"\\ddraw_Dll.dll").c_str());
    if (!library) return 4;
    const auto initialize = resolve<int (__cdecl*)()>(library, "sgl_InitHardware");
    const auto screen = resolve<void* (__cdecl*)(HWND, int, const char*, void*)>(library, "sgl_InitScreen");
    const auto source = resolve<std::intptr_t (__cdecl*)(std::intptr_t)>(library, "sgl_SetRenderSource");
    const auto target = resolve<std::intptr_t (__cdecl*)(std::intptr_t)>(library, "sgl_SetRenderDest");
    const auto destroy = resolve<void (__cdecl*)()>(library, "sgl_DestroyActiveScreen");
    const auto shutdown = resolve<void (__cdecl*)()>(library, "sgl_ShotDown");
    const auto create_clipper = resolve<void* (__cdecl*)()>(library, "sgl_CreateClipper");
    const auto set_clipper = resolve<void* (__cdecl*)(void*)>(library, "sgl_SetActiveClipper");
    const auto free_clipper = resolve<void (__cdecl*)(void*)>(library, "sgl_FreeClipper");
    const auto add_clip = resolve<void (__cdecl*)(const RECT*)>(library, "sgl_AddClipRect_r");
    const auto sub_clip = resolve<void (__cdecl*)(const RECT*)>(library, "sgl_SubClipRect_r");
    const auto clip_info = resolve<void (__cdecl*)(std::int32_t*)>(library, "sgl_GetActiveClipperInfo");
    const auto clip_mode = resolve<int (__cdecl*)(int)>(library, "sgl_Set2dClippingMode");
    const auto visible = resolve<int (__cdecl*)(const RECT*)>(library, "sgl_IsRectInClippingRegion");
    const auto lock_texture = resolve<int (__cdecl*)(std::intptr_t, LockOutput*, int)>(library, "sgl_LockTexture");
    const auto unlock_texture = resolve<void (__cdecl*)(std::intptr_t)>(library, "sgl_UnlockTexture");
    const auto region_blit = resolve<void (__cdecl*)(int)>(library, "sgl_Blt_RC");
    const auto dimensions = resolve<void (__cdecl*)(int, int, int, int, float, float)>(library, "sgl_SetScreenDimensions");
    const auto clear_buffer = resolve<void (__cdecl*)(unsigned, std::uint32_t, float)>(library, "sgl_ClearBuffer");
    if (!initialize || !screen || !source || !target || !destroy || !shutdown || !initialize()) return 5;
    std::array<std::uint32_t, 6> settings = {800, 600, 32, 60, 0, 0};
    void* handle = screen(window, 0, nullptr, settings.data());
    if (!handle) return 6;
    if (!create_clipper || !set_clipper || !free_clipper || !add_clip || !sub_clip || !clip_info || !clip_mode || !visible || !lock_texture || !unlock_texture || !region_blit || !clear_buffer) return 13;
    for (unsigned flags : {0u, 1u, 2u, 3u}) {
        LockOutput output{};
        if (!lock_texture(-4, &output, 2) || !output.pixels) return 23;
        for (int y = 0; y < 8; ++y) std::memset(output.pixels + y * output.pitch, 0x5a, 32);
        unlock_texture(-4);
        clear_buffer(flags, 0xff123456, 1.0f);
        if (!lock_texture(-4, &output, 1) || !output.pixels) return 24;
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
            std::uint32_t pixel;
            std::memcpy(&pixel, output.pixels + y * output.pitch + x * 4, 4);
            if (pixel != ((flags & 1) ? 0xff123456u : 0x5a5a5a5au)) return 25;
        }
        unlock_texture(-4);
    }
    std::puts("Offscreen clear flags 0..3 match color/depth expectations.");
    target(-3);
    const auto sgl_base = reinterpret_cast<std::uintptr_t>(library);
    const auto screen_object = *reinterpret_cast<const std::uintptr_t*>(sgl_base + 0x6e984);
    if (!screen_object) return 26;
    const auto table = *reinterpret_cast<const std::uintptr_t*>(screen_object);
    if (!table || *reinterpret_cast<const std::uintptr_t*>(table + 0x64) != sgl_base + 0x21e60) return 27;
    for (unsigned flags : {0u, 1u, 2u, 3u}) clear_buffer(flags, 0xff123456, 1.0f);
    target(-4);
    std::puts("Display clear dispatch matches the observed no-op; flags 0..3 returned.");
    void* region = create_clipper();
    if (!region) return 14;
    void* previous = set_clipper(region);
    const auto check_region = [&](const std::array<std::int32_t, 5>& expected) {
        std::array<std::int32_t, 5> info{};
        clip_info(info.data());
        std::printf("clipper info words: %d %d %d %d %d\n", info[0], info[1], info[2], info[3], info[4]);
        return info == expected;
    };
    if (!check_region({0, 0, 0, 0, 0})) return 15;
    const RECT first{1, 1, 7, 6}, second{5, 3, 10, 9}, removed{3, 2, 6, 5};
    add_clip(&first);
    if (!check_region({1, 0, 1, 0, 0})) return 16;
    add_clip(&second);
    if (!check_region({2, 0, 3, 0, 0})) return 17;
    sub_clip(&removed);
    if (!check_region({2, 1, 6, 0, 0})) return 18;
    clip_mode(2);
    unsigned mismatches = 0;
    for (int y = 0; y < 11; ++y) {
        for (int x = 0; x < 12; ++x) {
            const RECT pixel{x, y, x + 1, y + 1};
            const bool actual = visible(&pixel) != 0;
            const bool expected = ((x >= 1 && x < 7 && y >= 1 && y < 6) ||
                (x >= 5 && x < 10 && y >= 3 && y < 9)) && !(x >= 3 && x < 6 && y >= 2 && y < 5);
            if (actual != expected) ++mismatches;
        }
    }
    std::printf("Clipper pixel membership mismatches: %u\n", mismatches);
    if (mismatches) return 19;
    LockOutput pixels{};
    if (!lock_texture(-4, &pixels, 2) || !pixels.pixels) return 20;
    for (int y = 0; y < 33; ++y) for (int x = 0; x < 48; ++x) {
        const std::uint32_t color = 0xff000000 | (x * 17 << 16) | (y * 19 << 8) | (x + y);
        std::memcpy(pixels.pixels + y * pixels.pitch + x * 4, &color, 4);
    }
    unlock_texture(-4);
    source(-4);
    target(-3);
    region_blit(0);
    source(0);
    target(-4);
    if (!dimensions) return 21;
    const auto base_address = reinterpret_cast<std::uintptr_t>(library);
    const auto global = [base_address](std::uintptr_t offset) {
        return *reinterpret_cast<const std::int32_t*>(base_address + offset);
    };
    const auto actual_width = global(0x6e8a0), actual_height = global(0x6e89c);
    for (const auto& logical : std::array<std::array<int, 2>, 2>{{{800, 600}, {1024, 768}}}) {
        dimensions(0, 0, logical[0], logical[1], 1.0f, 1000.0f);
        const auto scale_x = global(0x6e924), scale_y = global(0x6e920);
        std::printf("Projection %dx%d / %dx%d: 16.16 scales %d, %d\n", actual_width, actual_height,
            logical[0], logical[1], scale_x, scale_y);
        if (scale_x != (static_cast<std::int64_t>(actual_width) << 16) / logical[0] ||
            scale_y != (static_cast<std::int64_t>(actual_height) << 16) / logical[1]) return 22;
    }
    set_clipper(previous);
    free_clipper(region);
    RECT current_client = {};
    if (!GetClientRect(window, &current_client)) return 10;
    std::printf("Requested %ux%u; client %ldx%ld -> %ldx%ld\n", settings[0], settings[1],
        initial_client.right, initial_client.bottom, current_client.right, current_client.bottom);
    const auto base = reinterpret_cast<std::uintptr_t>(library);
    for (std::uintptr_t offset : {0x80b34u, 0x80b38u, 0x80b3cu, 0x6e980u, 0x6e984u}) {
        std::printf("global +%05x = %08x\n", static_cast<unsigned>(offset),
            *reinterpret_cast<std::uint32_t*>(base + offset));
    }
    const auto describe_surface = [base](std::uintptr_t offset) {
        const auto pointer = *reinterpret_cast<const std::uintptr_t*>(base + offset);
        if (!pointer) return;
        std::uint16_t width, height;
        std::uint32_t pixels, pitch;
        std::memcpy(&pixels, reinterpret_cast<const void*>(pointer + 0x18), sizeof(pixels));
        std::memcpy(&width, reinterpret_cast<const void*>(pointer + 0x20), sizeof(width));
        std::memcpy(&height, reinterpret_cast<const void*>(pointer + 0x22), sizeof(height));
        std::memcpy(&pitch, reinterpret_cast<const void*>(pointer + 0x28), sizeof(pitch));
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(pointer);
        std::printf("  %ux%u pitch=%u pixels=%08x flags=%02x blend=%02x\n", width, height, pitch, pixels,
            *reinterpret_cast<const std::uint8_t*>(pointer + 0x34), *reinterpret_cast<const std::uint8_t*>(pointer + 0x37));
        std::printf("  blt=%08x blt_rc=%08x\n",
            *reinterpret_cast<const std::uint32_t*>(vtable + 0x48) - static_cast<std::uint32_t>(base),
            *reinterpret_cast<const std::uint32_t*>(vtable + 0x4c) - static_cast<std::uint32_t>(base));
    };
    const std::array<std::intptr_t, 4> values = {-1, -3, -4, 0};
    const std::array<std::intptr_t, 4> source_previous = {0, -4, -3, -4};
    const std::array<std::intptr_t, 4> target_previous = {-4, -4, -3, -4};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto value = values[i];
        const auto old = source(value);
        if (old != source_previous[i]) return 11;
        std::printf("source %d previous %d\n", static_cast<int>(value), static_cast<int>(old));
        describe_surface(0x6e980);
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto value = values[i];
        const auto old = target(value);
        if (old != target_previous[i]) return 12;
        std::printf("target %d previous %d\n", static_cast<int>(value), static_cast<int>(old));
        describe_surface(0x6e984);
    }
    destroy();
    shutdown();
    FreeLibrary(library);
    DestroyWindow(window);
    DEVMODEW after = {};
    after.dmSize = sizeof(after);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &after)) return 7;
    if (before.dmPelsWidth != after.dmPelsWidth || before.dmPelsHeight != after.dmPelsHeight ||
        before.dmDisplayFrequency != after.dmDisplayFrequency) return 8;
    std::printf("Desktop mode unchanged: %lux%lu at %lu Hz.\n", before.dmPelsWidth, before.dmPelsHeight, before.dmDisplayFrequency);
    return 0;
}
