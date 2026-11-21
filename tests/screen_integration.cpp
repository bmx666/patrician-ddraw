#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include "qhd_scroll_clips.hpp"

template<typename T> T resolve(HMODULE library, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(library, name));
}

struct LockOutput { void* pixels; void* palette; std::uint32_t width, height, pitch; };

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    const HMODULE library = LoadLibraryW(argv[1]);
    if (!library) return 2;
    const auto init = resolve<void* (__cdecl*)(HWND, int, const char*, const std::uint32_t*)>(library, "sgl_InitScreen");
    const auto destroy = resolve<void (__cdecl*)()>(library, "sgl_DestroyActiveScreen");
    const auto shutdown = resolve<void (__cdecl*)()>(library, "sgl_ShotDown");
    const auto cleanup = resolve<void (__cdecl*)()>(library, "sgl_CleanUp");
    const auto swap = resolve<int (__cdecl*)(std::uint32_t)>(library, "sgl_SwapBuffer");
    const auto switch_mode = resolve<int (__cdecl*)(const std::uint32_t*)>(library, "sgl_SwitchScreenMode");
    const auto source = resolve<void* (__cdecl*)(void*)>(library, "sgl_SetRenderSource");
    const auto target = resolve<void* (__cdecl*)(void*)>(library, "sgl_SetRenderDest");
    const auto create = resolve<void* (__cdecl*)(std::uint16_t, std::uint16_t, std::uint32_t)>(library, "sgl_CreateMemoryTexture");
    const auto free_image = resolve<void (__cdecl*)(void*)>(library, "sgl_FreeMemoryTexture");
    const auto free_texture = resolve<void (__cdecl*)(void*)>(library, "sgl_FreeTexture");
    const auto lock = resolve<int (__cdecl*)(void*, LockOutput*, int)>(library, "sgl_LockTexture");
    const auto unlock = resolve<void (__cdecl*)(void*)>(library, "sgl_UnlockTexture");
    const auto clipping = resolve<int (__cdecl*)(int)>(library, "sgl_Set2dClippingMode");
    const auto rectangle = resolve<void (__cdecl*)(int, int, int, int)>(library, "sgl_SetClipRect_4i");
    const auto draw = resolve<void (__cdecl*)(int, int, int, int, int, int)>(library, "sgl_DrawBitmapRect");
    const auto blit = resolve<void (__cdecl*)(int, int, int, int, int, int, std::uint32_t)>(library, "sgl_Blt");
    const auto clear = resolve<void (__cdecl*)(std::uint32_t, std::uint32_t, float)>(library, "sgl_ClearBuffer");
    const auto set_constant_color = resolve<std::uint32_t (__cdecl*)(std::uint32_t)>(library, "sgl_SetConstantColor");
    const auto fill = resolve<void (__cdecl*)(int, int, int, int)>(library, "sgl_FillSolidRect");
    const auto create_clipper = resolve<void* (__cdecl*)()>(library, "sgl_CreateClipper");
    const auto set_clipper = resolve<void* (__cdecl*)(void*)>(library, "sgl_SetActiveClipper");
    const auto free_clipper = resolve<void (__cdecl*)(void*)>(library, "sgl_FreeClipper");
    const auto add_clip = resolve<void (__cdecl*)(int, int, int, int)>(library, "sgl_AddClipRect_4i");
    const auto region_blit = resolve<void (__cdecl*)(std::uint32_t)>(library, "sgl_Blt_RC");
    const auto reset_clipper = resolve<void (__cdecl*)()>(library, "sgl_ResetClipper");
    if (!init || !destroy || !shutdown || !cleanup || !swap || !switch_mode || !source || !target || !create || !free_image || !free_texture || !lock || !unlock || !clipping || !rectangle || !draw || !blit || !clear || !set_constant_color || !fill || !create_clipper || !set_clipper || !free_clipper || !add_clip || !region_blit || !reset_clipper) return 3;
    cleanup();
    const std::array<std::uint32_t, 6> settings = {800, 600, 32, 60, 0, 0};
    if (switch_mode(settings.data()) != 0) return 24;
    DEVMODEW before = {};
    before.dmSize = sizeof(before);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &before)) return 4;
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 800, 600,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return 5;
    if (init(nullptr, 0, nullptr, settings.data())) return 6;
    void* screen = init(window, 0, nullptr, settings.data());
    if (!screen || switch_mode(settings.data()) != 1) return 7;
    auto invalid_mode = settings;
    invalid_mode[0] = 0;
    if (switch_mode(nullptr) != 0 || switch_mode(invalid_mode.data()) != 0) return 28;
    invalid_mode[0] = 1200;
    if (switch_mode(invalid_mode.data()) != 0) return 29;
    invalid_mode = settings;
    invalid_mode[0] = 1024;
    invalid_mode[1] = 768;
    if (switch_mode(invalid_mode.data()) != 0) return 40;
    auto wide_mode = settings;
    wide_mode[0] = 1920;
    wide_mode[1] = 1080;
    if (switch_mode(wide_mode.data()) != 1) return 30;
    const std::array<std::uint32_t, 6> options_mode_16 = {800, 600, 16, 60, 0, 0};
    if (switch_mode(options_mode_16.data()) != 1 || swap(2) != 0) return 31;
    LockOutput legacy_options_surface = {};
    if (!lock(reinterpret_cast<void*>(-4), &legacy_options_surface, 1) ||
        legacy_options_surface.width != wide_mode[0] || legacy_options_surface.height != wide_mode[1] ||
        legacy_options_surface.pitch < legacy_options_surface.width * 4) return 41;
    unlock(reinterpret_cast<void*>(-4));
    const std::array<std::uint32_t, 6> options_mode = {800, 600, 32, 60, 0, 0};
    if (switch_mode(options_mode.data()) != 1) return 39;
    invalid_mode = options_mode_16;
    invalid_mode[2] = 8;
    if (switch_mode(invalid_mode.data()) != 0) return 32;
    const std::array<std::array<std::uint32_t, 6>, 4> configured_modes = {{
        {1280, 1024, 32, 60, 0, 0},
        {1600, 900, 32, 60, 0, 0},
        {1920, 1080, 32, 60, 0, 0},
        {2560, 1440, 32, 60, 0, 0}
    }};
    for (const auto& mode : configured_modes) {
        if (switch_mode(mode.data()) != 1 || swap(2) != 0) return 33;
        LockOutput resized = {};
        if (!lock(reinterpret_cast<void*>(-4), &resized, 1) ||
            resized.width != mode[0] || resized.height != mode[1]) return 35;
        unlock(reinterpret_cast<void*>(-4));
    }
    const auto& last_mode = configured_modes.back();
    auto legacy_high_mode = last_mode;
    legacy_high_mode[2] = 16;
    if (switch_mode(legacy_high_mode.data()) != 1 || swap(2) != 0) return 42;
    LockOutput legacy_high_surface = {};
    if (!lock(reinterpret_cast<void*>(-4), &legacy_high_surface, 1) ||
        legacy_high_surface.width != last_mode[0] || legacy_high_surface.height != last_mode[1] ||
        legacy_high_surface.pitch < legacy_high_surface.width * 4) return 43;
    unlock(reinterpret_cast<void*>(-4));
    if (switch_mode(options_mode.data()) != 1 || swap(2) != 0 ||
        switch_mode(settings.data()) != 1 || swap(2) != 0) return 34;
    LockOutput after_options = {};
    if (!lock(reinterpret_cast<void*>(-4), &after_options, 1) ||
        after_options.width != last_mode[0] || after_options.height != last_mode[1]) return 36;
    unlock(reinterpret_cast<void*>(-4));
    const auto& render_mode = configured_modes[3];
    if (switch_mode(render_mode.data()) != 1 || swap(2) != 0) return 37;
    std::array<LockOutput, 2> screen_locks = {};
    const std::array<void*, 2> aliases = {reinterpret_cast<void*>(-4), reinterpret_cast<void*>(-3)};
    for (std::size_t i = 0; i < aliases.size(); ++i) {
        LockOutput screen_lock = {};
        if (!lock(aliases[i], &screen_lock, 1) || !screen_lock.pixels || !screen_lock.width ||
            !screen_lock.height || screen_lock.pitch < screen_lock.width * 4) return 23;
        screen_locks[i] = screen_lock;
        unlock(aliases[i]);
    }
    if (screen_locks[0].pixels == screen_locks[1].pixels || screen_locks[0].width != screen_locks[1].width ||
        screen_locks[0].height != screen_locks[1].height || screen_locks[0].pitch != screen_locks[1].pitch ||
        screen_locks[0].width != render_mode[0] || screen_locks[0].height != render_mode[1]) return 23;
    const auto screen_pattern = [](unsigned x, unsigned y, std::uint32_t salt) {
        return 0xff000000u | (((x * 29u + salt) & 0xffu) << 16) |
            (((y * 47u + salt * 3u) & 0xffu) << 8) | ((x * 17u + y * 31u + salt) & 0xffu);
    };
    const unsigned screen_width = screen_locks[1].width;
    const unsigned screen_height = screen_locks[1].height;
    std::vector<std::uint32_t> display_before(static_cast<std::size_t>(screen_width) * screen_height);
    LockOutput copy_surface = {};
    if (!lock(reinterpret_cast<void*>(-3), &copy_surface, 2) || !copy_surface.pixels ||
        copy_surface.width != screen_width || copy_surface.height != screen_height) return 50;
    for (unsigned y = 0; y < screen_height; ++y) {
        auto* row = static_cast<std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
        for (unsigned x = 0; x < screen_width; ++x) {
            const auto pixel = screen_pattern(x, y, 13);
            display_before[static_cast<std::size_t>(y) * screen_width + x] = pixel;
            std::memcpy(row + x * 4, &pixel, sizeof(pixel));
        }
    }
    unlock(reinterpret_cast<void*>(-3));
    void* saved_source = source(nullptr);
    void* saved_target = target(nullptr);
    source(reinterpret_cast<void*>(-3));
    target(reinterpret_cast<void*>(-3));
    blit(0, 174, 0, 42, 2276, 1266, 4);
    if (!lock(reinterpret_cast<void*>(-3), &copy_surface, 1) || !copy_surface.pixels) return 51;
    for (unsigned y = 0; y < screen_height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
        for (unsigned x = 0; x < screen_width; ++x) {
            std::uint32_t actual = 0;
            std::memcpy(&actual, row + x * 4, sizeof(actual));
            const unsigned source_y = y + 132;
            const auto expected = x < 2276 && y >= 42 && y < 1308
                ? display_before[static_cast<std::size_t>(source_y) * screen_width + x]
                : display_before[static_cast<std::size_t>(y) * screen_width + x];
            if (actual != expected) {
                unlock(reinterpret_cast<void*>(-3));
                return 52;
            }
        }
    }
    unlock(reinterpret_cast<void*>(-3));
    source(saved_source);
    target(saved_target);
    const auto write_pattern = [&](void* handle, std::uint32_t salt) {
        LockOutput surface_output = {};
        if (!lock(handle, &surface_output, 2) || !surface_output.pixels ||
            surface_output.width != screen_width || surface_output.height != screen_height) return false;
        for (unsigned y = 0; y < screen_height; ++y) {
            auto* row = static_cast<std::uint8_t*>(surface_output.pixels) + y * surface_output.pitch;
            for (unsigned x = 0; x < screen_width; ++x) {
                const auto pixel = screen_pattern(x, y, salt);
                std::memcpy(row + x * 4, &pixel, sizeof(pixel));
            }
        }
        unlock(handle);
        return true;
    };
    if (!write_pattern(reinterpret_cast<void*>(-1), 37) ||
        !write_pattern(reinterpret_cast<void*>(-3), 91)) return 53;
    const auto clipper = create_clipper();
    if (!clipper) return 54;
    void* previous_clipper = set_clipper(clipper);
    const int previous_clipping = clipping(2);
    for (unsigned i = 0; i < 413; ++i) {
        const int x = 5 + static_cast<int>(i % 26) * 96;
        const int y = 7 + static_cast<int>(i / 26) * 88;
        add_clip(x, y, x + 51, y + 37);
    }
    source(reinterpret_cast<void*>(-1));
    target(reinterpret_cast<void*>(-3));
    region_blit(0);
    if (!lock(reinterpret_cast<void*>(-3), &copy_surface, 1) || !copy_surface.pixels) return 55;
    for (unsigned y = 0; y < screen_height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
        for (unsigned x = 0; x < screen_width; ++x) {
            const unsigned column = x >= 5 ? (x - 5) / 96 : 26;
            const unsigned band = y >= 7 ? (y - 7) / 88 : 16;
            const bool copied = column < 26 && band < 16 && band * 26 + column < 413 &&
                x >= 5 + column * 96 && x < 56 + column * 96 &&
                y >= 7 + band * 88 && y < 44 + band * 88;
            const auto expected = screen_pattern(x, y, copied ? 37 : 91);
            std::uint32_t actual = 0;
            std::memcpy(&actual, row + x * 4, sizeof(actual));
            if (actual != expected) {
                unlock(reinterpret_cast<void*>(-3));
                return 56;
            }
        }
    }
    unlock(reinterpret_cast<void*>(-3));
    reset_clipper();
    if (!write_pattern(reinterpret_cast<void*>(-1), 37) ||
        !write_pattern(reinterpret_cast<void*>(-3), 91)) return 69;
    std::vector<std::uint8_t> captured_mask(static_cast<std::size_t>(screen_width) * screen_height, 0);
    for (const RECT& r : qhd_scroll_clips) {
        add_clip(r.left, r.top, r.right, r.bottom);
        const LONG left = std::clamp<LONG>(r.left, 0, static_cast<LONG>(screen_width));
        const LONG right = std::clamp<LONG>(r.right, 0, static_cast<LONG>(screen_width));
        const LONG top = std::clamp<LONG>(r.top, 0, static_cast<LONG>(screen_height));
        const LONG bottom = std::clamp<LONG>(r.bottom, 0, static_cast<LONG>(screen_height));
        for (LONG y = top; y < bottom; ++y) {
            std::fill(captured_mask.begin() + static_cast<std::size_t>(y) * screen_width + left,
                captured_mask.begin() + static_cast<std::size_t>(y) * screen_width + right, std::uint8_t{1});
        }
    }
    region_blit(0);
    if (!lock(reinterpret_cast<void*>(-3), &copy_surface, 1) || !copy_surface.pixels) return 70;
    for (unsigned y = 0; y < screen_height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
        for (unsigned x = 0; x < screen_width; ++x) {
            const auto expected = screen_pattern(x, y, captured_mask[static_cast<std::size_t>(y) * screen_width + x] ? 37 : 91);
            std::uint32_t actual = 0;
            std::memcpy(&actual, row + x * 4, sizeof(actual));
            if (actual != expected) {
                unlock(reinterpret_cast<void*>(-3));
                return 71;
            }
        }
    }
    unlock(reinterpret_cast<void*>(-3));
    reset_clipper();
    for (unsigned i = 0; i < 413; ++i) {
        const int x = 5 + static_cast<int>(i % 26) * 96;
        const int y = 7 + static_cast<int>(i / 26) * 88;
        add_clip(x, y, x + 51, y + 37);
    }
    void* indexed_sprite = create(static_cast<std::uint16_t>(screen_width),
        static_cast<std::uint16_t>(screen_height), 0x20);
    if (!indexed_sprite) return 57;
    LockOutput sprite_pixels = {};
    if (!lock(indexed_sprite, &sprite_pixels, 2) || !sprite_pixels.pixels) return 58;
    for (unsigned y = 0; y < screen_height; ++y) {
        auto* row = static_cast<std::uint8_t*>(sprite_pixels.pixels) + y * sprite_pixels.pitch;
        for (unsigned x = 0; x < screen_width; ++x) row[x] = static_cast<std::uint8_t>(x * 13u + y * 29u);
    }
    unlock(indexed_sprite);
    if (!lock(reinterpret_cast<void*>(-1), &copy_surface, 2) || !copy_surface.pixels) return 59;
    for (unsigned y = 0; y < screen_height; ++y) {
        auto* row = static_cast<std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
        for (unsigned x = 0; x < screen_width; ++x) {
            constexpr std::uint32_t background = 0xff102030u;
            std::memcpy(row + x * 4, &background, sizeof(background));
        }
    }
    unlock(reinterpret_cast<void*>(-1));
    source(indexed_sprite);
    target(reinterpret_cast<void*>(-1));
    draw(0, 0, 0, 0, static_cast<int>(screen_width), static_cast<int>(screen_height));
    if (!lock(reinterpret_cast<void*>(-1), &copy_surface, 1) || !copy_surface.pixels) return 60;
    for (unsigned y = 0; y < screen_height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
        for (unsigned x = 0; x < screen_width; ++x) {
            const unsigned column = x >= 5 ? (x - 5) / 96 : 26;
            const unsigned band = y >= 7 ? (y - 7) / 88 : 16;
            const bool copied = column < 26 && band < 16 && band * 26 + column < 413 &&
                x >= 5 + column * 96 && x < 56 + column * 96 &&
                y >= 7 + band * 88 && y < 44 + band * 88;
            const auto index = static_cast<std::uint8_t>(x * 13u + y * 29u);
            const auto expected = copied ? 0xff000000u | (static_cast<std::uint32_t>(index) * 0x010101u) : 0xff102030u;
            std::uint32_t actual = 0;
            std::memcpy(&actual, row + x * 4, sizeof(actual));
            if (actual != expected) {
                unlock(reinterpret_cast<void*>(-1));
                return 61;
            }
        }
    }
    unlock(reinterpret_cast<void*>(-1));
    reset_clipper();
    for (const RECT& r : qhd_scroll_clips) add_clip(r.left, r.top, r.right, r.bottom);
    if (!write_pattern(reinterpret_cast<void*>(-1), 91)) return 72;
    source(indexed_sprite);
    target(reinterpret_cast<void*>(-1));
    draw(0, 0, 0, 0, static_cast<int>(screen_width), static_cast<int>(screen_height));
    if (!lock(reinterpret_cast<void*>(-1), &copy_surface, 1) || !copy_surface.pixels) return 73;
    for (unsigned y = 0; y < screen_height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
        for (unsigned x = 0; x < screen_width; ++x) {
            const auto index = static_cast<std::uint8_t>(x * 13u + y * 29u);
            const auto expected = captured_mask[static_cast<std::size_t>(y) * screen_width + x]
                ? 0xff000000u | (static_cast<std::uint32_t>(index) * 0x010101u) : screen_pattern(x, y, 91);
            std::uint32_t actual = 0;
            std::memcpy(&actual, row + x * 4, sizeof(actual));
            if (actual != expected) {
                unlock(reinterpret_cast<void*>(-1));
                return 74;
            }
        }
    }
    unlock(reinterpret_cast<void*>(-1));
    clipping(previous_clipping);
    set_clipper(previous_clipper);
    free_clipper(clipper);
    source(saved_source);
    target(saved_target);
    free_texture(indexed_sprite);
    constexpr unsigned map_width = 2276;
    constexpr unsigned map_height = 1398;
    constexpr int view_top = 42;
    void* map_texture = create(static_cast<std::uint16_t>(map_width), static_cast<std::uint16_t>(map_height), 0);
    if (!map_texture) return 62;
    LockOutput map_pixels = {};
    if (!lock(map_texture, &map_pixels, 2) || !map_pixels.pixels ||
        map_pixels.width != map_width || map_pixels.height != map_height || map_pixels.pitch < map_width * 4)
        return 63;
    for (unsigned y = 0; y < map_height; ++y) {
        auto* row = static_cast<std::uint8_t*>(map_pixels.pixels) + y * map_pixels.pitch;
        for (unsigned x = 0; x < map_width; ++x) {
            const auto pixel = screen_pattern(x, y, 107);
            std::memcpy(row + x * 4, &pixel, sizeof(pixel));
        }
    }
    unlock(map_texture);
    constexpr std::uint32_t untouched = 0xff102030u;
    const int map_clipping = clipping(0);
    source(map_texture);
    // The second offset is the near-edge position captured in live frame 21.
    const std::array<std::array<int, 2>, 2> scroll_offsets = {{{238, 308}, {2106, 330}}};
    for (const auto& offset : scroll_offsets) {
        const int scroll_x = offset[0];
        const int scroll_y = offset[1];
        const int split_x = static_cast<int>(map_width) - scroll_x;
        const int split_y = static_cast<int>(map_height) - scroll_y;
        target(nullptr);
        if (!lock(reinterpret_cast<void*>(-4), &copy_surface, 2) || !copy_surface.pixels) return 64;
        for (unsigned y = 0; y < screen_height; ++y) {
            auto* row = static_cast<std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
            for (unsigned x = 0; x < screen_width; ++x) std::memcpy(row + x * 4, &untouched, sizeof(untouched));
        }
        unlock(reinterpret_cast<void*>(-4));
        target(reinterpret_cast<void*>(-4));
        draw(0, 0, scroll_x, view_top + split_y, split_x, scroll_y);
        draw(0, scroll_y, scroll_x, view_top, split_x, split_y);
        draw(split_x, 0, 0, view_top + split_y, scroll_x, scroll_y);
        draw(split_x, scroll_y, 0, view_top, scroll_x, split_y);
        if (!lock(reinterpret_cast<void*>(-4), &copy_surface, 1) || !copy_surface.pixels) return 65;
        for (unsigned y = 0; y < screen_height; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(copy_surface.pixels) + y * copy_surface.pitch;
            for (unsigned x = 0; x < screen_width; ++x) {
                const bool in_map_view = x < map_width && y >= view_top;
                const auto source_x = (x + map_width - scroll_x) % map_width;
                const auto source_y = (y + map_height - (view_top + split_y)) % map_height;
                const auto expected = in_map_view ? screen_pattern(source_x, source_y, 107) : untouched;
                std::uint32_t actual = 0;
                std::memcpy(&actual, row + x * 4, sizeof(actual));
                if (actual != expected) {
                    unlock(reinterpret_cast<void*>(-4));
                    return 66;
                }
            }
        }
        unlock(reinterpret_cast<void*>(-4));
    }
    clipping(map_clipping);
    source(saved_source);
    target(saved_target);
    free_texture(map_texture);
    constexpr std::uint32_t marker = 0xff39a7d2;
    target(reinterpret_cast<void*>(-4));
    const auto previous_constant_color = set_constant_color(marker);
    fill(700, 500, 10, 10);
    LockOutput mapped_screen = {};
    if (!lock(reinterpret_cast<void*>(-4), &mapped_screen, 1) || !mapped_screen.pixels) return 26;
    const double scale = std::min(static_cast<double>(mapped_screen.width) / render_mode[0],
        static_cast<double>(mapped_screen.height) / render_mode[1]);
    const unsigned sample_x = static_cast<unsigned>((mapped_screen.width - render_mode[0] * scale) / 2.0 + 705 * scale);
    const unsigned sample_y = static_cast<unsigned>((mapped_screen.height - render_mode[1] * scale) / 2.0 + 505 * scale);
    std::uint32_t sample = 0;
    std::memcpy(&sample, static_cast<const std::uint8_t*>(mapped_screen.pixels) + sample_y * mapped_screen.pitch + sample_x * 4, 4);
    unlock(reinterpret_cast<void*>(-4));
    set_constant_color(previous_constant_color);
    if (sample != marker) return 27;
    const auto previous_fill_color = set_constant_color(0xffaabbccu);
    fill(700, 500, 10, 10);
    set_constant_color(0x28000000u);
    fill(700, 500, 10, 10);
    if (!lock(reinterpret_cast<void*>(-4), &mapped_screen, 1) || !mapped_screen.pixels) return 44;
    const unsigned blended_x = static_cast<unsigned>((mapped_screen.width - render_mode[0] * scale) / 2.0 + 705 * scale);
    const unsigned blended_y = static_cast<unsigned>((mapped_screen.height - render_mode[1] * scale) / 2.0 + 505 * scale);
    std::memcpy(&sample, static_cast<const std::uint8_t*>(mapped_screen.pixels) +
        blended_y * mapped_screen.pitch + blended_x * 4, 4);
    unlock(reinterpret_cast<void*>(-4));
    set_constant_color(previous_fill_color);
    if (sample != 0xff8e9dabu) return 45;
    if (source(reinterpret_cast<void*>(-1)) != nullptr ||
        source(reinterpret_cast<void*>(-3)) != reinterpret_cast<void*>(-4) ||
        source(reinterpret_cast<void*>(-4)) != reinterpret_cast<void*>(-3) ||
        source(nullptr) != reinterpret_cast<void*>(-4) ||
        target(reinterpret_cast<void*>(-1)) != reinterpret_cast<void*>(-4) ||
        target(reinterpret_cast<void*>(-3)) != reinterpret_cast<void*>(-4) ||
        target(reinterpret_cast<void*>(-4)) != reinterpret_cast<void*>(-3) ||
        target(nullptr) != reinterpret_cast<void*>(-4)) return 15;
    if (target(reinterpret_cast<void*>(-1)) != nullptr) return 16;
    void* image = create(2, 2, 0);
    if (!image) return 8;
    target(image);
    for (std::uint32_t flags : {0u, 1u, 2u, 3u}) {
        LockOutput check{};
        if (!lock(image, &check, 2)) return 18;
        for (unsigned y = 0; y < 2; ++y) std::memset(static_cast<std::uint8_t*>(check.pixels) + y * check.pitch, 0x5a, 8);
        unlock(image);
        clear(flags, 0xff123456, 1.0f);
        if (!lock(image, &check, 1)) return 19;
        for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 2; ++x) {
            std::uint32_t pixel;
            std::memcpy(&pixel, static_cast<const std::uint8_t*>(check.pixels) + y * check.pitch + x * 4, 4);
            if (pixel != ((flags & 1) ? 0xff123456u : 0x5a5a5a5au)) return 20;
        }
        unlock(image);
    }
    target(reinterpret_cast<void*>(-1));
    LockOutput out = {};
    if (!lock(image, &out, 2) || !out.pixels) return 9;
    const std::array<std::uint32_t, 4> pixels = {0xff123456, 0xffabcdef, 0xff324252, 0xff987654};
    std::memcpy(out.pixels, pixels.data(), 8);
    std::memcpy(static_cast<std::uint8_t*>(out.pixels) + out.pitch, pixels.data() + 2, 8);
    unlock(image);
    if (source(image) != nullptr) return 10;
    clipping(1);
    rectangle(0, 0, 2, 2);
    draw(0, 0, 0, 0, 2, 2);
    if (source(reinterpret_cast<void*>(-1)) != image || target(reinterpret_cast<void*>(-3)) != reinterpret_cast<void*>(-4)) return 17;
    blit(0, 0, 0, 0, 2, 2, 2);
    clear(3, 0xffabcdef, 1.0f);
    source(reinterpret_cast<void*>(-3));
    target(image);
    blit(0, 0, 0, 0, 2, 2, 0);
    if (!lock(image, &out, 1)) return 21;
    for (unsigned y = 0; y < 2; ++y) {
        if (std::memcmp(static_cast<const std::uint8_t*>(out.pixels) + y * out.pitch, pixels.data() + y * 2, 8)) return 22;
    }
    unlock(image);
    void* gpu_source = create(2, 2, 0);
    void* gpu_intermediate = create(2, 2, 0);
    void* gpu_result = create(2, 2, 0);
    if (!gpu_source || !gpu_intermediate || !gpu_result) return 46;
    if (!lock(gpu_source, &out, 2)) return 47;
    std::memcpy(out.pixels, pixels.data(), 8);
    std::memcpy(static_cast<std::uint8_t*>(out.pixels) + out.pitch, pixels.data() + 2, 8);
    unlock(gpu_source);
    source(gpu_source);
    target(gpu_intermediate);
    draw(0, 0, 0, 0, 2, 2);
    source(gpu_intermediate);
    target(gpu_result);
    draw(0, 0, 0, 0, 2, 2);
    if (!lock(gpu_result, &out, 1)) return 48;
    for (unsigned y = 0; y < 2; ++y) {
        if (std::memcmp(static_cast<const std::uint8_t*>(out.pixels) + y * out.pitch,
            pixels.data() + y * 2, 8)) return 49;
    }
    unlock(gpu_result);
    source(nullptr);
    target(nullptr);
    free_texture(gpu_source);
    free_texture(gpu_intermediate);
    free_texture(gpu_result);
    source(reinterpret_cast<void*>(-1));
    target(reinterpret_cast<void*>(-3));
    if (swap(2) != 0) return 11;
    if (source(nullptr) != reinterpret_cast<void*>(-4) || target(nullptr) != reinterpret_cast<void*>(-3)) return 12;
    clear(2, 0, 1.0f);
    clear(0, 0, 1.0f);
    free_image(image);
    void* texture = create(1, 1, 0);
    if (!texture || source(texture) != nullptr || target(texture) != nullptr) return 44;
    free_texture(texture);
    if (source(nullptr) != nullptr || target(nullptr) != nullptr) return 45;
    destroy();
    if (!init(window, 0, nullptr, settings.data())) return 13;
    cleanup();
    shutdown();
    DestroyWindow(window);
    FreeLibrary(library);
    DEVMODEW after = {};
    after.dmSize = sizeof(after);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &after) || before.dmPelsWidth != after.dmPelsWidth ||
        before.dmPelsHeight != after.dmPelsHeight || before.dmDisplayFrequency != after.dmDisplayFrequency) return 14;
    return 0;
}
