#include "archive_state.hpp"
#include <atomic>

namespace {
std::atomic<void*> archive_pointer{nullptr};
}

void* sgl::active_archive() noexcept { return archive_pointer.load(std::memory_order_relaxed); }

extern "C" void* __cdecl sgl_SetArchive(void* archive) noexcept {
    return archive_pointer.exchange(archive, std::memory_order_relaxed);
}
