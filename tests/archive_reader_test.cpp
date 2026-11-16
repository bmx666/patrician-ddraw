#include "archive_reader.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    sgl::ArchiveReader reader;
    if (FAILED(reader.initialize(std::filesystem::path(argv[1])))) return 2;
    std::vector<std::uint8_t> output = {42};
    if (reader.read(nullptr, "complete", output) != S_OK ||
        output != std::vector<std::uint8_t>({'a', 'b', 'c', 'd', 'e'})) return 3;
    output = {42};
    if (reader.read(nullptr, "mapped", output) != S_OK ||
        output != std::vector<std::uint8_t>({'a', 'b', 'c', 'd', 'e'})) return 4;
    output = {42};
    if (reader.read(nullptr, "truncated", output) != E_FAIL || output != std::vector<std::uint8_t>{42}) return 5;
    if (reader.read(nullptr, "missing", output) != E_FAIL || output != std::vector<std::uint8_t>{42}) return 6;
    return 0;
}
