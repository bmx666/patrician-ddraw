#include "unimplemented.hpp"

#include <windows.h>
#include <cstring>

namespace sgl {
[[noreturn]] void unimplemented(const char* name) noexcept {
    OutputDebugStringA("Unimplemented SGL export: ");
    OutputDebugStringA(name);
    OutputDebugStringA("\n");
    const char prefix[] = "Unimplemented SGL export: ";
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    const bool reported = error != nullptr && error != INVALID_HANDLE_VALUE &&
        WriteFile(error, prefix, sizeof(prefix) - 1, &written, nullptr) &&
        WriteFile(error, name, static_cast<DWORD>(std::strlen(name)), &written, nullptr) &&
        WriteFile(error, "\n", 1, &written, nullptr);
    if (!reported) MessageBoxA(nullptr, name, "Unimplemented SGL export", MB_OK | MB_ICONERROR | MB_TASKMODAL);
    ExitProcess(ERROR_PROC_NOT_FOUND);
}
}
