#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct Pixels {
    std::uint8_t* data;
    std::uint32_t* palette;
    std::uint32_t width, height, pitch;
};
static_assert(sizeof(Pixels) == 20);

struct Sgl {
    HMODULE library;
    template<class T> T symbol(const char* name) const {
        auto value = reinterpret_cast<T>(GetProcAddress(library, name));
        if (!value) throw std::runtime_error(name);
        return value;
    }
    int (__cdecl* initialize)() = symbol<decltype(initialize)>("sgl_InitHardware");
    void* (__cdecl* create)(std::uint16_t, std::uint16_t, std::uint32_t) = symbol<decltype(create)>("sgl_CreateMemoryTexture");
    int (__cdecl* lock)(void*, Pixels*, int) = symbol<decltype(lock)>("sgl_LockTexture");
    void (__cdecl* unlock)(void*) = symbol<decltype(unlock)>("sgl_UnlockTexture");
    void* (__cdecl* source)(void*) = symbol<decltype(source)>("sgl_SetRenderSource");
    void* (__cdecl* target)(void*) = symbol<decltype(target)>("sgl_SetRenderDest");
    std::uint32_t (__cdecl* color)(std::uint32_t) = symbol<decltype(color)>("sgl_SetConstantColor");
    int (__cdecl* mode)(int) = symbol<decltype(mode)>("sgl_Set2dClippingMode");
    void (__cdecl* bounds)(int, int, int, int) = symbol<decltype(bounds)>("sgl_SetClipRect");
    void* (__cdecl* create_clipper)() = symbol<decltype(create_clipper)>("sgl_CreateClipper");
    void* (__cdecl* clipper)(void*) = symbol<decltype(clipper)>("sgl_SetActiveClipper");
    void (__cdecl* reset)() = symbol<decltype(reset)>("sgl_ResetClipper");
    void (__cdecl* copy)(void*) = symbol<decltype(copy)>("sgl_CopyClipper");
    void (__cdecl* add)(const RECT*) = symbol<decltype(add)>("sgl_AddClipRect_r");
    void (__cdecl* subtract)(const RECT*) = symbol<decltype(subtract)>("sgl_SubClipRect_r");
    void (__cdecl* palette)(void*, const void*) = symbol<decltype(palette)>("sgl_ChangePalette");
    void (__cdecl* draw)(int, int, int, int, int, int) = symbol<decltype(draw)>("sgl_DrawBitmapRect");
    void (__cdecl* stretch)(int, int, int, int, int, int, int, int) = symbol<decltype(stretch)>("sgl_StretchBitmapRect");
};

std::vector<std::uint8_t> read_pixels(const std::filesystem::path& path, std::size_t size) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open resource: " + path.string());
    std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    if (bytes.size() != size) throw std::runtime_error("Incorrect resource size: " + path.string());
    return bytes;
}

struct Pair { void* old_value; void* new_value; };
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4 || argc > 5 || (argc == 5 && std::wstring(argv[4]) != L"--final-only")) {
        std::fputs("Usage: recorded_draw_oracle <original game directory> <replacement DLL> <fixture> [--final-only]\n", stderr);
        return 2;
    }
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    try {
        const std::filesystem::path fixture_path = std::filesystem::absolute(argv[3]);
        std::ifstream input(fixture_path);
        std::string version;
        unsigned width, height, flags;
        if (!(input >> version >> width >> height >> flags) || version != "P3_DRAW_V1" ||
            !width || !height || width > 16384 || height > 16384 || (flags & 0x20))
            throw std::runtime_error("Invalid draw fixture header");
        if (!SetDllDirectoryW(argv[1]) || !SetCurrentDirectoryW(argv[1])) return 3;
        Sgl old{LoadLibraryW((std::filesystem::path(argv[1]) / L"ddraw_Dll.dll").c_str())};
        Sgl current{LoadLibraryW(argv[2])};
        if (old.library == current.library || !old.initialize() || !current.initialize()) return 4;
        Pair target{old.create(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), flags),
            current.create(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), flags)};
        Pixels before{}, after{};
        if (!target.old_value || !target.new_value || !old.lock(target.old_value, &before, 2) ||
            !current.lock(target.new_value, &after, 2)) return 5;
        for (unsigned y = 0; y < height; ++y) {
            auto* a = reinterpret_cast<std::uint32_t*>(before.data + y * before.pitch);
            auto* b = reinterpret_cast<std::uint32_t*>(after.data + y * after.pitch);
            for (unsigned x = 0; x < width; ++x) a[x] = b[x] = 0xff102030u;
        }
        old.unlock(target.old_value);
        current.unlock(target.new_value);
        old.target(target.old_value);
        current.target(target.new_value);
        std::unordered_map<unsigned, Pair> resources, clippers;
        auto clipper = [&](unsigned id) -> Pair {
            auto entry = clippers.find(id);
            if (entry != clippers.end()) return entry->second;
            Pair value{old.create_clipper(), current.create_clipper()};
            if (!value.old_value || !value.new_value) throw std::runtime_error("Clipper allocation failed");
            clippers.emplace(id, value);
            return value;
        };
        unsigned call = 0, draws = 0;
        auto compare = [&]() {
            if (!old.lock(target.old_value, &before, 1) || !current.lock(target.new_value, &after, 1))
                throw std::runtime_error("Target readback failed");
            std::size_t changed = 0;
            for (unsigned y = 0; y < height; ++y) {
                const auto* a = reinterpret_cast<const std::uint32_t*>(before.data + y * before.pitch);
                const auto* b = reinterpret_cast<const std::uint32_t*>(after.data + y * after.pitch);
                for (unsigned x = 0; x < width; ++x) {
                    if (a[x] != b[x]) {
                        std::fprintf(stderr, "First difference: call=%u pixel=(%u,%u) original=%08x replacement=%08x.\n",
                            call, x, y, a[x], b[x]);
                        throw std::runtime_error("Pixel mismatch");
                    }
                    changed += a[x] != 0xff102030u;
                }
            }
            old.unlock(target.old_value); current.unlock(target.new_value);
            return changed;
        };
        std::string command;
        while (input >> call >> command) {
            if (command == "resource") {
                unsigned id, w, h, pitch, resource_flags, blend;
                std::string pixels_path, palette_path;
                input >> id >> w >> h >> pitch >> resource_flags >> blend >> pixels_path >> palette_path;
                if (!input || resources.count(id) || !w || !h || w > 65535 || h > 65535 || blend > 1 ||
                    pitch < w * ((resource_flags & 0x20) ? 1u : 4u))
                    throw std::runtime_error("Invalid resource record");
                const auto pixels = read_pixels(fixture_path.parent_path() / pixels_path,
                    static_cast<std::size_t>(pitch) * h);
                Pair value{old.create(static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), resource_flags),
                    current.create(static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), resource_flags)};
                if (!value.old_value || !value.new_value) return 6;
                if (resource_flags & 0x20) {
                    const auto palette = read_pixels(fixture_path.parent_path() / palette_path, 1024);
                    old.palette(value.old_value, palette.data());
                    current.palette(value.new_value, palette.data());
                }
                Pixels a{}, b{};
                if (!old.lock(value.old_value, &a, 2) || !current.lock(value.new_value, &b, 2)) return 7;
                const unsigned row_size = w * ((resource_flags & 0x20) ? 1u : 4u);
                for (unsigned y = 0; y < h; ++y) {
                    std::memcpy(a.data + y * a.pitch, pixels.data() + y * pitch, row_size);
                    std::memcpy(b.data + y * b.pitch, pixels.data() + y * pitch, row_size);
                }
                old.unlock(value.old_value);
                current.unlock(value.new_value);
                const auto old_blend = *(static_cast<const std::uint8_t*>(value.old_value) + 0x37);
                const auto new_blend = *(static_cast<const std::uint8_t*>(value.new_value) + 0x37);
                if (old_blend != blend || new_blend != blend) throw std::runtime_error("Unrecorded blend transition");
                resources.emplace(id, value);
            } else if (command == "source") {
                unsigned id; input >> id;
                const auto value = resources.at(id);
                old.source(value.old_value); current.source(value.new_value);
            } else if (command == "color") {
                std::uint32_t value; input >> value;
                old.color(value); current.color(value);
            } else if (command == "mode") {
                int value; input >> value;
                old.mode(value); current.mode(value);
            } else if (command == "bounds") {
                int l, t, r, b; input >> l >> t >> r >> b;
                old.bounds(l, t, r, b); current.bounds(l, t, r, b);
            } else if (command == "active") {
                unsigned id; input >> id;
                const auto value = clipper(id);
                old.clipper(value.old_value); current.clipper(value.new_value);
            } else if (command == "copy") {
                unsigned id; input >> id;
                const auto value = clipper(id);
                old.copy(value.old_value); current.copy(value.new_value);
            } else if (command == "reset") {
                old.reset(); current.reset();
            } else if (command == "add" || command == "subtract") {
                RECT rect; input >> rect.left >> rect.top >> rect.right >> rect.bottom;
                if (command == "add") { old.add(&rect); current.add(&rect); }
                else { old.subtract(&rect); current.subtract(&rect); }
            } else if (command == "draw" || command == "stretch") {
                int sx, sy, dx, dy, w, h; input >> sx >> sy >> dx >> dy >> w >> h;
                if (command == "draw") {
                    old.draw(sx, sy, dx, dy, w, h); current.draw(sx, sy, dx, dy, w, h);
                } else {
                    int dw, dh; input >> dw >> dh;
                    old.stretch(sx, sy, dx, dy, w, h, dw, dh);
                    current.stretch(sx, sy, dx, dy, w, h, dw, dh);
                }
                if (!input) throw std::runtime_error("Invalid draw record");
                if (argc == 4) compare();
                ++draws;
            } else throw std::runtime_error("Unknown fixture command: " + command);
            if (!input) throw std::runtime_error("Truncated fixture command: " + command);
        }
        if (!input.eof() || !draws) throw std::runtime_error("Incomplete draw fixture");
        const auto changed = compare();
        if (!changed) throw std::runtime_error("No recorded draw changed a pixel");
        std::printf("%u recorded draws match %s (%ux%u target, %zu resources, %zu changed pixels).\n",
            draws, argc == 4 ? "after every call" : "without intermediate readbacks", width, height, resources.size(), changed);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Recorded draw comparison failed: %s\n", error.what());
        return 10;
    }
}
