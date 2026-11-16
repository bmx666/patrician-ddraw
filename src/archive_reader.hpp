#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace sgl {

class ArchiveReader {
public:
    ArchiveReader() = default;
    ~ArchiveReader();
    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    HRESULT initialize(const std::filesystem::path& library_path) noexcept;
    HRESULT read(void* container, const char* name, std::vector<std::uint8_t>& output) noexcept;

private:
    HMODULE library_ = nullptr;
    void* (__cdecl* map_)(void*, const char*, void**, int*) = nullptr;
    int (__cdecl* unmap_)(void*, void*) = nullptr;
    void* (__cdecl* open_)(void*, const char*) = nullptr;
    int (__cdecl* length_)(void*) = nullptr;
    int (__cdecl* read_)(void*, void*, int) = nullptr;
    void (__cdecl* close_)(void*) = nullptr;
};

}
