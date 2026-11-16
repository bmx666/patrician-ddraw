#include "legacy_render_state.hpp"

namespace {
sgl::LegacyRenderState state;
}

namespace sgl {
const LegacyRenderState& legacy_render_state() noexcept {
    return state;
}
}

extern "C" std::uint32_t __cdecl sgl_SetZBufferMode(std::uint32_t mode) {
    const std::uint32_t previous = state.z_buffer_mode;
    state.z_buffer_mode = mode & 0xffu;
    return previous & 0xffu;
}

extern "C" bool __cdecl sgl_SetZWrite(int enabled) {
    const bool previous = state.z_write_mode == 0xbu;
    state.z_write_mode = enabled != 0 ? 0xbu : 0xcu;
    return previous;
}

extern "C" bool __cdecl sgl_EnableFog(int enabled) {
    const bool previous = state.fog_mode == 7;
    state.fog_mode = enabled != 0 ? 7u : 8u;
    return previous;
}

extern "C" std::uint32_t __cdecl sgl_EnableBackfaceCulling(std::uint32_t enabled) {
    const std::uint8_t requested = static_cast<std::uint8_t>(enabled);
    if (requested == state.backface_culling) return enabled;

    const std::uint32_t previous = state.backface_culling;
    state.backface_culling = requested;
    return previous;
}

extern "C" void __cdecl sgl_SetPolygonDrawMode(int mode) {
    if (mode == 0 || mode == 1) state.polygon_draw_mode = static_cast<std::uint32_t>(mode);
}
