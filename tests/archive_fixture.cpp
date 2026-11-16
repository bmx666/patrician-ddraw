#include <algorithm>
#include <cstring>

namespace {
struct Stream { int offset; bool truncated; bool open; };
Stream stream{};
constexpr char content[] = "abcde";
}

extern "C" void* __cdecl arc_CreateMemMapped(void*, const char* name, void** data, int* size) {
    if (std::strcmp(name, "mapped")) return nullptr;
    *data = const_cast<char*>(content);
    *size = 5;
    return const_cast<char*>(name);
}
extern "C" int __cdecl arc_ReleaseMemMapped(void*, void*) { return 0; }
extern "C" void* __cdecl arc_CreateFile(void*, const char* name) {
    if (stream.open || (std::strcmp(name, "complete") && std::strcmp(name, "truncated"))) return nullptr;
    stream = {0, std::strcmp(name, "truncated") == 0, true};
    return &stream;
}
extern "C" int __cdecl arc_FileLength(void*) { return 5; }
extern "C" int __cdecl arc_Read(void* file, void* output, int length) {
    auto* current = static_cast<Stream*>(file);
    if (current->truncated && current->offset >= 2) return 0;
    const int count = std::min({2, length, 5 - current->offset});
    std::memcpy(output, content + current->offset, count);
    current->offset += count;
    return count;
}
extern "C" void __cdecl arc_ReleaseFile(void*) { stream.open = false; }
