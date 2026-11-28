#include "image_decoder.hpp"

#include <array>
#include <atomic>
#include <climits>
#include <cstdint>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

namespace {
std::mutex aim_mutex;
thread_local bool decoder_active = false;
std::atomic<bool> aim_direct{true};
}

namespace sgl {

bool image_decoder_active() noexcept { return decoder_active; }

void set_aim_direct(bool enabled) noexcept { aim_direct.store(enabled, std::memory_order_relaxed); }

ImageDecoder::~ImageDecoder() {
    if (library_) FreeLibrary(library_);
}

HRESULT ImageDecoder::initialize(const std::filesystem::path& library_path) noexcept {
    if (library_ || !library_path.is_absolute()) return E_INVALIDARG;
    const HMODULE library = LoadLibraryExW(library_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library) return HRESULT_FROM_WIN32(GetLastError());
    const auto initialize = reinterpret_cast<decltype(initialize_)>(GetProcAddress(library, "?AIM_INIT@@YAXPAUAIM_IMAGE@@@Z"));
    const auto decode = reinterpret_cast<decltype(decode_)>(GetProcAddress(library, "?AIM_CONVERT_MEMFILE@@YAHPAUAIM_IMAGE@@PBDPAEJ@Z"));
    const auto inspect = reinterpret_cast<decltype(inspect_)>(GetProcAddress(library, "?AIM_GETFILEFORMAT_FROMMEM@@YAHPAUAIM_GFXDEF@@PBDPAEJ@Z"));
    const auto release = reinterpret_cast<decltype(release_)>(GetProcAddress(library, "?AIM_FREE@@YAXPAUAIM_IMAGE@@@Z"));
    const auto convert = reinterpret_cast<decltype(convert_)>(GetProcAddress(library, "?AIM_CONVERT_RAW@@YAHPAUAIM_IMAGE@@@Z"));
    if (!initialize || !decode || !inspect || !release || !convert) {
        FreeLibrary(library);
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    library_ = library;
    initialize_ = initialize;
    decode_ = decode;
    inspect_ = inspect;
    release_ = release;
    convert_ = convert;
    // Optional: only the patrician-mods AIM.dll has it.
    direct_ = reinterpret_cast<decltype(direct_)>(GetProcAddress(library, "AIM_DECODE_DIRECT"));
    return S_OK;
}

namespace {
struct DirectAllocation {
    std::vector<std::uint8_t>* pixels;
    bool failed;
};

std::uint8_t* __cdecl allocate_direct(void* context, std::uint32_t bytes) noexcept {
    auto& allocation = *static_cast<DirectAllocation*>(context);
    try {
        allocation.pixels->resize(bytes);
        return allocation.pixels->data();
    } catch (...) {
        allocation.failed = true;
        return nullptr;
    }
}
}

HRESULT ImageDecoder::process_direct(const char* name, const std::vector<std::uint8_t>& encoded, DecodedImage& output,
    bool pixels) noexcept {
    std::lock_guard<std::mutex> lock(aim_mutex);
    try {
        DecodedImage decoded;
        std::array<std::uint8_t, 1024> palette{};
        DirectImage image{0, 0, 0, 0, palette.data()};
        DirectAllocation allocation{&decoded.pixels, false};
        decoder_active = true;
        const int result = direct_(name, encoded.data(), static_cast<std::int32_t>(encoded.size()), pixels ? 1 : 0,
            &image, &allocate_direct, &allocation);
        decoder_active = false;
        last_layout_ = {image.format == 1 ? 1u : 4u, image.format, image.pitch};
        if (result < 0 || allocation.failed) return E_OUTOFMEMORY;
        if (result == 0) return E_FAIL;
        const bool indexed = image.format == 1;
        if ((!indexed && image.format != 4) || !image.width || !image.height || image.width > 65535 ||
            image.height > 65535 || image.pitch != (indexed ? (image.width + 3u) & ~3u : image.width * 4u) ||
            (pixels && decoded.pixels.size() < static_cast<std::size_t>(image.pitch) * image.height)) return E_NOTIMPL;
        decoded.width = image.width;
        decoded.height = image.height;
        decoded.pitch = image.pitch;
        decoded.format = indexed ? ImageFormat::Indexed8 : ImageFormat::Bgra32;
        if (pixels && indexed) decoded.palette.assign(palette.begin(), palette.end());
        output = std::move(decoded);
        return S_OK;
    } catch (const std::bad_alloc&) {
        decoder_active = false;
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        decoder_active = false;
        return E_OUTOFMEMORY;
    }
}

HRESULT qol_picture(const char* name, void* container, bool pixels, DecodedImage& output) noexcept {
    // The layout of AIM_DECODE_DIRECT's picture.
    struct Picture {
        std::uint32_t width, height, pitch, format;
        std::uint8_t* palette;
    };
    using Make = int (__cdecl*)(const char*, void*, std::int32_t, Picture*, std::uint8_t* (__cdecl*)(void*, std::uint32_t),
        void*);
    if (!name || !*name) return S_FALSE;
    const HMODULE qol = GetModuleHandleW(L"qol.dll");
    if (!qol) return S_FALSE;
    const auto make = reinterpret_cast<Make>(GetProcAddress(qol, "QOL_PICTURE"));
    if (!make) return S_FALSE;
    try {
        DecodedImage decoded;
        std::array<std::uint8_t, 1024> palette{};
        Picture image{0, 0, 0, 0, palette.data()};
        DirectAllocation allocation{&decoded.pixels, false};
        const int result = make(name, container, pixels ? 1 : 0, &image, &allocate_direct, &allocation);
        if (result < 0 || allocation.failed) return E_OUTOFMEMORY;
        if (result == 0) return S_FALSE;
        if (image.format != 1 || !image.width || !image.height || image.width > 65535 || image.height > 65535 ||
            image.pitch != ((image.width + 3u) & ~3u) ||
            (pixels && decoded.pixels.size() < static_cast<std::size_t>(image.pitch) * image.height)) return E_NOTIMPL;
        decoded.width = image.width;
        decoded.height = image.height;
        decoded.pitch = image.pitch;
        decoded.format = ImageFormat::Indexed8;
        if (pixels) decoded.palette.assign(palette.begin(), palette.end());
        output = std::move(decoded);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return E_OUTOFMEMORY;
    }
}

HRESULT ImageDecoder::decode(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output) noexcept {
    return process(name, encoded, output, true);
}

HRESULT ImageDecoder::inspect(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output) noexcept {
    return process(name, encoded, output, false);
}

bool ImageDecoder::direct_path() const noexcept {
    return direct_ && aim_direct.load(std::memory_order_relaxed);
}

HRESULT ImageDecoder::process(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output, bool pixels) noexcept {
    if (!library_ || !name || !*name || encoded.empty() || encoded.size() > LONG_MAX) return E_INVALIDARG;
    if (direct_path()) return process_direct(name, encoded, output, pixels);
    std::lock_guard<std::mutex> lock(aim_mutex);
    std::array<std::uint32_t, 23> image = {};
    initialize_(image.data());
    decoder_active = true;
    const int result = (pixels ? decode_ : inspect_)(image.data(), name, encoded.data(), static_cast<long>(encoded.size()));
    decoder_active = false;
    HRESULT status = E_FAIL;
    last_layout_ = {image[4], image[5], image[6]};
    if (result) {
        const auto width = image[2];
        const auto height = image[3];
        const auto pitch = image[6];
        const bool bgra = image[4] == 4 && image[5] == 4;
        const bool indexed = image[4] == 1 && image[5] == 1 && (!pixels || image[1] != 0);
        const bool bgr24 = image[4] == 3 && image[5] == 6;
        const bool valid_size = width && height && width <= 65535 && height <= 65535;
        if (pixels && image[0] && valid_size && !bgra && !indexed && !bgr24) {
            // Other source formats (such as format 15 of the creature animations) are converted to 32-bit BGRA by
            // AIM_CONVERT_RAW, as the original library does: fields 11 to 17 describe the destination (pixels,
            // palette, width, height, bytes per pixel, format and pitch).
            try {
                DecodedImage decoded;
                decoded.width = width;
                decoded.height = height;
                decoded.pitch = width * 4;
                decoded.format = ImageFormat::Bgra32;
                decoded.pixels.assign(static_cast<std::size_t>(decoded.pitch) * height, 0);
                std::array<std::uint8_t, 1024> palette{};
                image[11] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(decoded.pixels.data()));
                image[12] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(palette.data()));
                image[13] = width;
                image[14] = height;
                image[15] = 4;
                image[16] = 4;
                image[17] = decoded.pitch;
                decoder_active = true;
                const int converted = convert_(image.data());
                decoder_active = false;
                image[11] = image[12] = 0;
                if (converted) {
                    output = std::move(decoded);
                    status = S_OK;
                } else {
                    status = E_NOTIMPL;
                }
            } catch (const std::bad_alloc&) {
                status = E_OUTOFMEMORY;
            } catch (const std::length_error&) {
                status = E_OUTOFMEMORY;
            }
        } else if (!pixels && valid_size) {
            const bool output_indexed = image[5] == 1;
            const auto output_bpp = output_indexed ? 1u : 4u;
            const auto output_pitch = (width * output_bpp + 3u) & ~3u;
            try {
                DecodedImage decoded;
                decoded.width = width;
                decoded.height = height;
                decoded.pitch = output_pitch;
                decoded.format = output_indexed ? ImageFormat::Indexed8 : ImageFormat::Bgra32;
                output = std::move(decoded);
                status = S_OK;
            } catch (const std::bad_alloc&) {
                status = E_OUTOFMEMORY;
            } catch (const std::length_error&) {
                status = E_OUTOFMEMORY;
            }
        } else if ((pixels && !image[0]) || !width || !height || width > 65535 || height > 65535 ||
            (!bgra && !indexed && !bgr24) || pitch < width * image[4] ||
            static_cast<std::uint64_t>(pitch) * height > SIZE_MAX) {
            status = E_NOTIMPL;
        } else {
            try {
                DecodedImage decoded;
                decoded.width = width;
                decoded.height = height;
                decoded.pitch = pitch;
                decoded.format = indexed ? ImageFormat::Indexed8 : bgr24 ? ImageFormat::Bgr24 : ImageFormat::Bgra32;
                if (pixels) {
                    const auto data = reinterpret_cast<const std::uint8_t*>(image[0]);
                    decoded.pixels.assign(data, data + static_cast<std::size_t>(pitch) * height);
                }
                if (pixels && indexed) {
                    const auto palette = reinterpret_cast<const std::uint8_t*>(image[1]);
                    decoded.palette.assign(palette, palette + 1024);
                }
                output = std::move(decoded);
                status = S_OK;
            } catch (const std::bad_alloc&) {
                status = E_OUTOFMEMORY;
            } catch (const std::length_error&) {
                status = E_OUTOFMEMORY;
            }
        }
    }
    release_(image.data());
    return status;
}

}
