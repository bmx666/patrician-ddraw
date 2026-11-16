#include "game_resolution.hpp"

int main() {
    constexpr sgl::DisplaySize original{1280, 1024};
    constexpr sgl::DisplaySize hd_plus{1600, 900};
    constexpr sgl::DisplaySize full_hd{1920, 1080};
    constexpr sgl::DisplaySize wide{2560, 1440};
    constexpr sgl::FastForwardMapLayoutState original_layout{0, 0, 1280, 1024, 1280, 1024};
    constexpr sgl::FastForwardMapLayoutState hd_plus_layout{0, 0, 1600, 900, 1600, 900};
    constexpr sgl::FastForwardMapLayoutState full_hd_layout{0, 0, 1920, 1080, 1920, 1080};
    constexpr sgl::FastForwardMapLayoutState wide_layout{0, 0, 2560, 1440, 2560, 1440};
    constexpr sgl::FastForwardMapLayoutState stale_frame{0, 0, 2560, 1440, 1280, 1024};
    constexpr sgl::FastForwardMapLayoutState stale_position{1, 0, 2560, 1440, 2560, 1440};

    if (!sgl::fast_forward_map_layout_matches(original_layout, original, original)) return 1;
    if (!sgl::fast_forward_map_layout_matches(hd_plus_layout, hd_plus, hd_plus)) return 2;
    if (!sgl::fast_forward_map_layout_matches(full_hd_layout, full_hd, full_hd)) return 3;
    if (!sgl::fast_forward_map_layout_matches(wide_layout, wide, wide)) return 4;
    if (sgl::fast_forward_map_layout_matches(stale_frame, wide, wide)) return 5;
    if (sgl::fast_forward_map_layout_matches(wide_layout, original, original)) return 6;
    if (sgl::fast_forward_map_layout_matches(stale_position, wide, wide)) return 7;

    return 0;
}
