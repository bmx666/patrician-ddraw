#include "image_decoder.hpp"

#include <array>
#include <climits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

namespace {
std::mutex aim_mutex;
thread_local bool decoder_active = false;
}

namespace sgl {

bool image_decoder_active() noexcept { return decoder_active; }

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
    if (!initialize || !decode || !inspect || !release) {
        FreeLibrary(library);
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    library_ = library;
    initialize_ = initialize;
    decode_ = decode;
    inspect_ = inspect;
    release_ = release;
    return S_OK;
}

HRESULT ImageDecoder::decode(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output) noexcept {
    return process(name, encoded, output, true);
}

HRESULT ImageDecoder::inspect(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output) noexcept {
    return process(name, encoded, output, false);
}

HRESULT ImageDecoder::process(const char* name, std::vector<std::uint8_t>& encoded, DecodedImage& output, bool pixels) noexcept {
    if (!library_ || !name || !*name || encoded.empty() || encoded.size() > LONG_MAX) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(aim_mutex);
    std::array<std::uint32_t, 23> image = {};
    initialize_(image.data());
    decoder_active = true;
    const int result = (pixels ? decode_ : inspect_)(image.data(), name, encoded.data(), static_cast<long>(encoded.size()));
    decoder_active = false;
    HRESULT status = E_FAIL;
    if (result) {
        const auto width = image[2];
        const auto height = image[3];
        const auto pitch = image[6];
        const bool bgra = image[4] == 4 && image[5] == 4;
        const bool indexed = image[4] == 1 && image[5] == 1 && (!pixels || image[1] != 0);
        const bool bgr24 = image[4] == 3 && image[5] == 6;
        if (!pixels && width && height && width <= 65535 && height <= 65535) {
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
