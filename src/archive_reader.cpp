#include "archive_reader.hpp"

#include <cstdio>
#include <new>
#include <stdexcept>
#include <utility>

namespace sgl {
namespace {

void report_read_failure(const char* stage, void* container, const char* name, long detail) noexcept {
    char message[384];
    std::snprintf(message, sizeof(message),
        "SGL archive read failed: %s, resource=%s container=%p detail=%ld\n",
        stage, name, container, detail);
    OutputDebugStringA(message);
}

}

ArchiveReader::~ArchiveReader() {
    if (library_) FreeLibrary(library_);
}

HRESULT ArchiveReader::initialize(const std::filesystem::path& library_path) noexcept {
    if (library_ || !library_path.is_absolute()) return E_INVALIDARG;
    const HMODULE library = LoadLibraryExW(library_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!library) return HRESULT_FROM_WIN32(GetLastError());
    const auto map = reinterpret_cast<decltype(map_)>(GetProcAddress(library, "arc_CreateMemMapped"));
    const auto unmap = reinterpret_cast<decltype(unmap_)>(GetProcAddress(library, "arc_ReleaseMemMapped"));
    const auto open = reinterpret_cast<decltype(open_)>(GetProcAddress(library, "arc_CreateFile"));
    const auto length = reinterpret_cast<decltype(length_)>(GetProcAddress(library, "arc_FileLength"));
    const auto read = reinterpret_cast<decltype(read_)>(GetProcAddress(library, "arc_Read"));
    const auto close = reinterpret_cast<decltype(close_)>(GetProcAddress(library, "arc_ReleaseFile"));
    if (!map || !unmap || !open || !length || !read || !close) {
        FreeLibrary(library);
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    library_ = library;
    map_ = map;
    unmap_ = unmap;
    open_ = open;
    length_ = length;
    read_ = read;
    close_ = close;
    return S_OK;
}

HRESULT ArchiveReader::read(void* container, const char* name, std::vector<std::uint8_t>& output) noexcept {
    if (!library_ || !name || !*name) return E_INVALIDARG;
    void* data = nullptr;
    int size = 0;
    void* handle = map_(container, name, &data, &size);
    if (!handle) {
        void* file = open_(container, name);
        if (!file) {
            report_read_failure("open", container, name, 0);
            return E_FAIL;
        }
        const int length = length_(file);
        HRESULT status = E_FAIL;
        std::vector<std::uint8_t> bytes;
        if (length >= 0) {
            try {
                bytes.resize(static_cast<std::size_t>(length));
                int offset = 0;
                while (offset < length) {
                    const int count = read_(file, bytes.data() + offset, length - offset);
                    if (count <= 0 || count > length - offset) break;
                    offset += count;
                }
                if (offset == length) status = S_OK;
            } catch (const std::bad_alloc&) {
                status = E_OUTOFMEMORY;
            } catch (const std::length_error&) {
                status = E_OUTOFMEMORY;
            }
        }
        close_(file);
        if (SUCCEEDED(status)) output = std::move(bytes);
        else report_read_failure("stream", container, name, length);
        return status;
    }
    HRESULT status = E_FAIL;
    std::vector<std::uint8_t> bytes;
    if (size >= 0 && (data || size == 0)) {
        try {
            if (size) {
                const auto start = static_cast<const std::uint8_t*>(data);
                bytes.assign(start, start + size);
            }
            status = S_OK;
        } catch (const std::bad_alloc&) {
            status = E_OUTOFMEMORY;
        } catch (const std::length_error&) {
            status = E_OUTOFMEMORY;
        }
    }
    (void)unmap_(container, handle);
    if (FAILED(status)) report_read_failure("mapped payload", container, name, size);
    if (SUCCEEDED(status)) output = std::move(bytes);
    return status;
}

}
