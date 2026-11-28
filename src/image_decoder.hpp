#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sgl {

bool image_decoder_active() noexcept;
// [display] aim_direct of high_res.ini: false makes every decoder take the AIM_CONVERT_MEMFILE path even when AIM.dll
// exports AIM_DECODE_DIRECT. Applies to decoders created afterwards and to calls of existing ones.
void set_aim_direct(bool enabled) noexcept;

enum class ImageFormat { Bgra32, Indexed8, Bgr24 };

constexpr std::uint32_t bytes_per_pixel(ImageFormat format) noexcept {
    switch (format) {
    case ImageFormat::Indexed8: return 1;
    case ImageFormat::Bgr24: return 3;
    case ImageFormat::Bgra32: return 4;
    }
    return 0;
}

struct DecodedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch = 0;
    ImageFormat format = ImageFormat::Bgra32;
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> palette;
};

// QOL_PICTURE of the patrician-mods qol.dll: a widescreen picture (top bar, carving, overview map) that qol.dll makes
// at run time, as 8-bit palette rows padded to 4 bytes and 256 BGRA palette entries, without a file. `container` is
// the archiver container the base pictures are read from. With `pixels` false only the size and format are filled.
// S_FALSE when qol.dll is not loaded, lacks the export or does not make `name`: the caller reads the file through the
// archiver then. `output` is changed only on S_OK.
HRESULT qol_picture(const char* name, void* container, bool pixels, DecodedImage& output) noexcept;

class ImageDecoder {
public:
    ImageDecoder() = default;
    ~ImageDecoder();
    ImageDecoder(const ImageDecoder&) = delete;
    ImageDecoder& operator=(const ImageDecoder&) = delete;

    HRESULT initialize(const std::filesystem::path& library_path) noexcept;
    HRESULT inspect(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output) noexcept;
    HRESULT decode(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output) noexcept;
    // AIM's pixel layout fields (bytes per pixel, format and pitch) of the last image, for failure messages.
    const std::array<std::uint32_t, 3>& last_layout() const noexcept { return last_layout_; }
    // Whether decode and inspect take AIM_DECODE_DIRECT now: exported by AIM.dll and not turned off by aim_direct=0.
    bool direct_path() const noexcept;

private:
    HRESULT process(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output, bool pixels) noexcept;
    HMODULE library_ = nullptr;
    void (__cdecl* initialize_)(void*) = nullptr;
    int (__cdecl* decode_)(void*, const char*, unsigned char*, long) = nullptr;
    int (__cdecl* inspect_)(void*, const char*, unsigned char*, long) = nullptr;
    int (__cdecl* convert_)(void*) = nullptr;
    void (__cdecl* release_)(void*) = nullptr;
    // AIM_DECODE_DIRECT of the patrician-mods AIM.dll (null for the original): decodes into the final layout.
    struct DirectImage {
        std::uint32_t width, height, pitch, format;
        std::uint8_t* palette;
    };
    using Allocate = std::uint8_t* (__cdecl*)(void* context, std::uint32_t bytes);
    int (__cdecl* direct_)(const char*, const std::uint8_t*, std::int32_t, std::int32_t, DirectImage*, Allocate, void*) = nullptr;
    HRESULT process_direct(const char* name, const std::vector<std::uint8_t>& encoded, DecodedImage& output, bool pixels) noexcept;
    std::array<std::uint32_t, 3> last_layout_{};
};

}
