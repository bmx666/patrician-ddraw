#include <windows.h>
#include <cstdint>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const HMODULE module = LoadLibraryA(argv[1]);
    if (module == nullptr) return 3;
    if (std::strcmp(argv[2], "lookup") == 0) {
        for (int index = 3; index < argc; ++index) {
            if (GetProcAddress(module, argv[index]) == nullptr) return 4;
        }
        return 0;
    }
    if (std::strcmp(argv[2], "invoke") == 0 && argc == 4) {
        const FARPROC entry = GetProcAddress(module, argv[3]);
        if (entry == nullptr) return 4;
        entry();
        return 5;
    }
    if ((std::strcmp(argv[2], "ret") == 0 || std::strcmp(argv[2], "zero") == 0) && argc == 4) {
        const auto entry = reinterpret_cast<const unsigned char*>(GetProcAddress(module, argv[3]));
        if (!entry) return 4;
        if (std::strcmp(argv[2], "ret") == 0) return entry[0] == 0xc3 ? 0 : 7;
        const unsigned char expected[] = {0x33, 0xc0, 0xc3};
        return std::memcmp(entry, expected, sizeof(expected)) == 0 ? 0 : 7;
    }
    if (std::strcmp(argv[2], "archive") == 0 && argc == 4) {
        const auto set_archive = reinterpret_cast<void* (__cdecl*)(void*)>(GetProcAddress(module, "sgl_SetArchive"));
        if (!set_archive) return 4;
        auto first = reinterpret_cast<void*>(0x12345678);
        auto second = reinterpret_cast<void*>(0x23456789);
        if (set_archive(first) != nullptr || set_archive(second) != first ||
            set_archive(nullptr) != second || set_archive(nullptr) != nullptr) return 6;
        return 0;
    }
    if (std::strcmp(argv[2], "texture_info") == 0 && argc == 4) {
        const auto info = reinterpret_cast<void (__cdecl*)(const void*, std::uint32_t*)>(GetProcAddress(module, "sgl_GetTextureInfo"));
        if (!info) return 4;
        unsigned char object[0x24] = {};
        object[0x20] = 0x34; object[0x21] = 0x12;
        object[0x22] = 0xcd; object[0x23] = 0xab;
        std::uint32_t output[] = {9, 9, 9};
        info(nullptr, output);
        if (output[0] || output[1] || output[2]) return 8;
        info(object, output);
        if (output[0] != 0x1234 || output[1] != 0xabcd || output[2] != reinterpret_cast<std::uintptr_t>(object)) return 8;
        return 0;
    }
    if (std::strcmp(argv[2], "blend_mode") == 0 && argc == 4) {
        const auto set_blend = reinterpret_cast<std::uint32_t (__cdecl*)(std::uint32_t)>(GetProcAddress(module, "sgl_SetBlendMode"));
        if (!set_blend) return 4;
        if ((set_blend(2) & 0xff) != 1 || (set_blend(1) & 0xff) != 2 ||
            (set_blend(1) & 0xff) != 1 || (set_blend(0x102) & 0xff) != 1 ||
            (set_blend(2) & 0xff) != 2) return 11;
        return 0;
    }
    if (std::strcmp(argv[2], "fill_solid_rect") == 0 && argc == 4) {
        struct LockOutput { void* pixels; void* palette; std::uint32_t width, height, pitch; };
        const auto create = reinterpret_cast<void* (__cdecl*)(std::uint16_t, std::uint16_t, std::uint32_t)>(GetProcAddress(module, "sgl_CreateMemoryTexture"));
        const auto set_destination = reinterpret_cast<void* (__cdecl*)(void*)>(GetProcAddress(module, "sgl_SetRenderDest"));
        const auto set_color = reinterpret_cast<std::uint32_t (__cdecl*)(std::uint32_t)>(GetProcAddress(module, "sgl_SetConstantColor"));
        const auto fill = reinterpret_cast<void (__cdecl*)(int, int, int, int)>(GetProcAddress(module, "sgl_FillSolidRect"));
        const auto lock = reinterpret_cast<int (__cdecl*)(void*, LockOutput*, int)>(GetProcAddress(module, "sgl_LockTexture"));
        const auto unlock = reinterpret_cast<void (__cdecl*)(void*)>(GetProcAddress(module, "sgl_UnlockTexture"));
        const auto free_texture = reinterpret_cast<void (__cdecl*)(void*)>(GetProcAddress(module, "sgl_FreeMemoryTexture"));
        if (!create || !set_destination || !set_color || !fill || !lock || !unlock || !free_texture) return 4;
        void* texture = create(4, 3, 0);
        if (!texture || set_destination(texture) != nullptr || set_color(0xff112233u) != 0) return 9;
        fill(-1, 1, 3, 1);
        LockOutput output{};
        if (!lock(texture, &output, 1) || output.width != 4 || output.height != 3 || output.pitch != 16) return 10;
        const auto* pixels = static_cast<const std::uint8_t*>(output.pixels);
        const std::uint8_t expected[] = {0x33, 0x22, 0x11, 0xff};
        const auto row = static_cast<std::size_t>(output.pitch);
        if (std::memcmp(pixels + row, expected, sizeof(expected)) != 0 ||
            std::memcmp(pixels + row + sizeof(expected), expected, sizeof(expected)) != 0 ||
            std::memcmp(pixels + row + 2 * sizeof(expected), "\0\0\0\0", 4) != 0 ||
            std::memcmp(pixels, "\0\0\0\0", 4) != 0) return 11;
        unlock(texture);
        if (set_color(0xffaabbccu) != 0xff112233u) return 12;
        fill(0, 0, 4, 3);
        if (set_color(0x28000000u) != 0xffaabbccu) return 13;
        fill(0, 0, 4, 3);
        if (!lock(texture, &output, 1)) return 14;
        const auto* blended_pixels = static_cast<const std::uint8_t*>(output.pixels);
        const std::uint32_t blended_black = 0xff8e9dabu;
        for (unsigned y = 0; y < output.height; ++y) for (unsigned x = 0; x < output.width; ++x) {
            std::uint32_t pixel = 0;
            std::memcpy(&pixel, blended_pixels + y * output.pitch + x * 4, sizeof(pixel));
            if (pixel != blended_black) return 15;
        }
        unlock(texture);
        if (set_color(0xffaabbccu) != 0x28000000u) return 16;
        fill(0, 0, 4, 3);
        if (set_color(0x28123456u) != 0xffaabbccu) return 19;
        fill(0, 0, 4, 3);
        if (!lock(texture, &output, 1)) return 17;
        std::uint32_t blended_color = 0;
        std::memcpy(&blended_color, output.pixels, sizeof(blended_color));
        unlock(texture);
        if (blended_color != 0xff90a5b8u) return 18;
        set_destination(nullptr);
        free_texture(texture);
        return 0;
    }
    if (std::strcmp(argv[2], "clip_rect") == 0 && argc == 4) {
        const auto set_clip = reinterpret_cast<void (__cdecl*)(int, int, int, int)>(GetProcAddress(module, "sgl_SetClipRect"));
        const auto set_clip_4i = reinterpret_cast<void (__cdecl*)(int, int, int, int)>(GetProcAddress(module, "sgl_SetClipRect_4i"));
        const auto get_clip = reinterpret_cast<void (__cdecl*)(std::int32_t*)>(GetProcAddress(module, "sgl_GetClipRect_r"));
        if (!set_clip || !set_clip_4i || !get_clip) return 4;
        std::int32_t clip[4] = {};
        set_clip(-17, 29, 311, 457);
        get_clip(clip);
        if (clip[0] != -17 || clip[1] != 29 || clip[2] != 311 || clip[3] != 457) return 13;
        set_clip_4i(2, 3, 401, 509);
        get_clip(clip);
        if (clip[0] != 2 || clip[1] != 3 || clip[2] != 401 || clip[3] != 509) return 14;
        return 0;
    }
    if (std::strcmp(argv[2], "legacy_render_state") == 0 && argc == 4) {
        using SetZBufferMode = std::uint32_t (__cdecl*)(std::uint32_t);
        using SetBooleanState = bool (__cdecl*)(int);
        using SetBackfaceCulling = std::uint32_t (__cdecl*)(std::uint32_t);
        using SetPolygonDrawMode = void (__cdecl*)(int);
        const auto set_z_buffer = reinterpret_cast<SetZBufferMode>(GetProcAddress(module, "sgl_SetZBufferMode"));
        const auto set_z_write = reinterpret_cast<SetBooleanState>(GetProcAddress(module, "sgl_SetZWrite"));
        const auto set_fog = reinterpret_cast<SetBooleanState>(GetProcAddress(module, "sgl_EnableFog"));
        const auto set_backface_culling = reinterpret_cast<SetBackfaceCulling>(GetProcAddress(module, "sgl_EnableBackfaceCulling"));
        const auto set_polygon_draw_mode = reinterpret_cast<SetPolygonDrawMode>(GetProcAddress(module, "sgl_SetPolygonDrawMode"));
        if (!set_z_buffer || !set_z_write || !set_fog || !set_backface_culling || !set_polygon_draw_mode) return 4;
        if (set_z_buffer(0x103) != 0 || set_z_buffer(0x200) != 3) return 15;
        if (set_z_write(1) || !set_z_write(-1) || !set_z_write(0)) return 16;
        if (set_fog(0) || set_fog(1) || !set_fog(2)) return 17;
        if (set_backface_culling(0x101) != 0xff ||
            set_backface_culling(0x100) != 1 || set_backface_culling(0x100) != 0x100) return 18;
        set_polygon_draw_mode(0);
        set_polygon_draw_mode(2);
        set_polygon_draw_mode(1);
        return 0;
    }
    if ((std::strcmp(argv[2], "image_flags_without_archiver") == 0 || std::strcmp(argv[2], "image_high_flags_without_archiver") == 0) && argc == 4) {
        const auto load = reinterpret_cast<void* (__cdecl*)(const char*, std::uint32_t)>(GetProcAddress(module, "sgl_LoadMemoryTexture"));
        if (!load) return 4;
        const std::uint32_t flags = std::strcmp(argv[2], "image_high_flags_without_archiver") == 0 ? 0x03000003u : 5u;
        load("image.bmp", flags);
        return 5;
    }
    if (std::strcmp(argv[2], "unsupported_lock") == 0 && argc == 4) {
        const auto lock = reinterpret_cast<int (__cdecl*)(void*, void*, int)>(GetProcAddress(module, "sgl_LockTexture"));
        if (!lock) return 4;
        lock(nullptr, nullptr, 4);
        return 5;
    }
    if (std::strcmp(argv[2], "unsupported_draw") == 0 && argc == 4) {
        const auto draw = reinterpret_cast<void (__cdecl*)(int, int, int, int, int, int)>(GetProcAddress(module, "sgl_DrawBitmapRect"));
        if (!draw) return 4;
        draw(0, 0, 0, 0, 1, 1);
        return 5;
    }
    if (std::strcmp(argv[2], "init_without_deps") == 0 && argc == 4) {
        const auto init = reinterpret_cast<int (__cdecl*)()>(GetProcAddress(module, "sgl_InitHardware"));
        if (!init) return 4;
        return init() == 0 ? 0 : 10;
    }
    if (std::strcmp(argv[2], "oversize_create") == 0 && argc == 4) {
        const auto create = reinterpret_cast<void* (__cdecl*)(std::uint16_t, std::uint16_t, std::uint32_t)>(GetProcAddress(module, "sgl_CreateMemoryTexture"));
        if (!create) return 4;
        return create(65535, 65535, 0) == nullptr ? 0 : 9;
    }
    return 2;
}
