#pragma once

#include <cstdint>
#include <optional>

namespace sgl {
struct LegacyRenderState {
    std::uint32_t z_buffer_mode = 0;
    std::uint32_t z_write_mode = 0;
    std::uint32_t fog_mode = 0;
    std::uint8_t backface_culling = 0xff;
    std::optional<std::uint32_t> polygon_draw_mode;
};

const LegacyRenderState& legacy_render_state() noexcept;
}

extern "C" std::uint32_t __cdecl sgl_SetZBufferMode(std::uint32_t mode);
extern "C" bool __cdecl sgl_SetZWrite(int enabled);
extern "C" bool __cdecl sgl_EnableFog(int enabled);
extern "C" std::uint32_t __cdecl sgl_EnableBackfaceCulling(std::uint32_t enabled);
extern "C" void __cdecl sgl_SetPolygonDrawMode(int mode);
