#pragma once

#include "display_config.hpp"
#include "image_decoder.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace sgl {
enum class DisplayApplyResult { Failed, Applied, AppliedNotSaved };

struct FastForwardMapLayoutState {
    std::int32_t x, y;
    std::uint32_t width, height;
    std::uint32_t frame_width, frame_height;
};

constexpr bool fast_forward_map_layout_matches(FastForwardMapLayoutState state,
    DisplaySize display, DisplaySize source) noexcept {
    return state.x == 0 && state.y == 0 &&
        state.width == display.width && state.height == display.height &&
        state.frame_width == source.width && state.frame_height == source.height;
}

bool install_game_resolution(DisplaySize size, std::string& error);
bool update_game_resolution(DisplaySize size) noexcept;
bool refresh_accelerated_map_layout() noexcept;
void remove_game_resolution() noexcept;
// Rewrites the minimum frame time of the game's frame limiter (FUN_004bd180, originally 20 ms). No value restores
// the original limiter. Requires an installed game resolution binding; a no-op for programs without one.
bool set_game_frame_limit(std::optional<unsigned> max_fps) noexcept;
DisplaySize pending_game_resolution() noexcept;
DisplayApplyResult apply_options_display_resolution(std::string& error);
DisplaySize current_game_resolution() noexcept;
DisplaySize fast_forward_map_source_size() noexcept;
void set_fast_forward_map_source_size(DisplaySize size) noexcept;
void adapt_game_chrome(const char* name, DecodedImage& image);
void adapt_game_chrome(const char* name, DecodedImage& image, DisplaySize size);
}
