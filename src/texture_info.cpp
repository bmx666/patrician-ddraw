#include <cstdint>
#include <cstring>

extern "C" void __cdecl sgl_GetTextureInfo(const void* texture, std::uint32_t* output) noexcept {
    if (!texture) {
        output[0] = output[1] = output[2] = 0;
        return;
    }
    const auto bytes = static_cast<const std::uint8_t*>(texture);
    std::uint16_t width, height;
    std::memcpy(&width, bytes + 0x20, sizeof(width));
    std::memcpy(&height, bytes + 0x22, sizeof(height));
    output[0] = width;
    output[1] = height;
    output[2] = reinterpret_cast<std::uintptr_t>(texture);
}
