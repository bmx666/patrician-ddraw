#pragma once

#include <windows.h>
#include <cstdint>

// The running game, identified by the link time of the executable loaded at its preferred base. Every other program,
// including the SGL test programs, keeps the Patrician III behaviour.
namespace sgl::game {

enum class Executable { Other, Patrician3 };

// Patrician3.exe of the GOG and Steam releases.
inline constexpr std::uint32_t patrician3_timestamp = 0x3f544d3c;

inline Executable executable() noexcept {
    static const Executable value = [] {
        const auto* const base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (reinterpret_cast<std::uintptr_t>(base) != 0x400000) return Executable::Other;
        const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return Executable::Other;
        const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
            return Executable::Other;
        if (nt->FileHeader.TimeDateStamp == patrician3_timestamp) return Executable::Patrician3;
        return Executable::Other;
    }();
    return value;
}

}
