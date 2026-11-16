#include "color_key.hpp"
#include <atomic>

namespace {
std::atomic<std::uint32_t> key{0};
}

std::uint32_t sgl::color_key() noexcept { return key.load(std::memory_order_relaxed); }
void sgl::initialize_color_key() noexcept { key.store(0xff000000, std::memory_order_relaxed); }

extern "C" std::uint32_t __cdecl sgl_SetColorKey(std::uint32_t value) noexcept {
    return key.exchange(value & 0x00ffffff, std::memory_order_relaxed);
}
