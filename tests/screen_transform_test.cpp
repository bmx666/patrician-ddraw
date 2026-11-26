#include "screen_transform.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

int main() {
    struct Case { unsigned width, height; double scale, x, y; };
    const std::array<Case, 5> cases = {{
        {1024, 768, 1.28, 0.0, 0.0},
        {1280, 1024, 1.6, 0.0, 32.0},
        {1600, 900, 1.5, 200.0, 0.0},
        {1920, 1080, 1.8, 240.0, 0.0},
        {2560, 1440, 2.4, 320.0, 0.0}
    }};
    for (const auto& item : cases) {
        sgl::ScreenTransform transform;
        if (!sgl::ScreenTransform::create(800, 600, item.width, item.height, transform) ||
            std::abs(transform.scale - item.scale) > 0.000001 ||
            std::abs(transform.offset_x - item.x) > 0.000001 ||
            std::abs(transform.offset_y - item.y) > 0.000001) return 1;
        const auto bounds = transform.map({0.0f, 0.0f, 800.0f, 600.0f});
        if (std::abs(bounds.left - item.x) > 0.0001 ||
            std::abs(bounds.top - item.y) > 0.0001 ||
            std::abs(bounds.right - (item.x + 800.0 * item.scale)) > 0.0001 ||
            std::abs(bounds.bottom - (item.y + 600.0 * item.scale)) > 0.0001) return 2;
        RECT clip{};
        if (!transform.map_clip({0, 0, 800, 600}, clip) ||
            clip.left != static_cast<LONG>(item.x) || clip.top != static_cast<LONG>(item.y) ||
            clip.right != static_cast<LONG>(item.x + 800.0 * item.scale) ||
            clip.bottom != static_cast<LONG>(item.y + 600.0 * item.scale)) return 3;
        float logical_x = 0.0f, logical_y = 0.0f;
        const float output_x = static_cast<float>(item.x + 400.0 * item.scale);
        const float output_y = static_cast<float>(item.y + 300.0 * item.scale);
        if (!transform.inverse(output_x, output_y, logical_x, logical_y) ||
            std::abs(logical_x - 400.0f) > 0.0001f || std::abs(logical_y - 300.0f) > 0.0001f) return 4;

        const float client_x = 2.0f * output_x - 0.5f;
        const float client_y = 2.0f * output_y - 0.5f;
        if (!transform.inverse_client_point(client_x, client_y, item.width * 2, item.height * 2,
            logical_x, logical_y) || std::abs(logical_x - 400.0f) > 0.0001f ||
            std::abs(logical_y - 300.0f) > 0.0001f) return 5;
    }

    struct OutputCase { unsigned width, height; double scale, x, y; };
    const std::array<OutputCase, 5> output_cases = {{
        {1024, 768, 1.875, 320.0, 0.0},
        {1280, 1024, 1.40625, 380.0, 0.0},
        {1600, 900, 1.6, 0.0, 0.0},
        {1920, 1080, 4.0 / 3.0, 0.0, 0.0},
        {2560, 1440, 1.0, 0.0, 0.0}
    }};
    for (const auto& item : output_cases) {
        sgl::ScreenTransform transform;
        if (!sgl::ScreenTransform::create(item.width, item.height, 2560, 1440, transform) ||
            std::abs(transform.scale - item.scale) > 0.000001 ||
            std::abs(transform.offset_x - item.x) > 0.000001 ||
            std::abs(transform.offset_y - item.y) > 0.000001) return 13;
        const auto viewport = transform.map({0.0f, 0.0f, static_cast<float>(item.width),
            static_cast<float>(item.height)});
        if (std::abs(viewport.left - item.x) > 0.0001 ||
            std::abs(viewport.top - item.y) > 0.0001 ||
            std::abs(viewport.right - (item.x + item.width * item.scale)) > 0.0001 ||
            std::abs(viewport.bottom - (item.y + item.height * item.scale)) > 0.0001) return 14;
        float render_x = 0.0f, render_y = 0.0f;
        if (!transform.inverse(static_cast<float>(item.x + item.width * item.scale / 2.0),
            static_cast<float>(item.y + item.height * item.scale / 2.0), render_x, render_y) ||
            std::abs(render_x - item.width / 2.0f) > 0.0001f ||
            std::abs(render_y - item.height / 2.0f) > 0.0001f) return 15;
    }

    sgl::ScreenTransform transform;
    if (!sgl::ScreenTransform::create(800, 600, 1600, 900, transform)) return 6;
    const auto fractional = transform.map({1.0f, 1.0f, 2.0f, 2.0f});
    RECT clip{};
    if (std::abs(fractional.left - 201.5f) > 0.0001f ||
        !transform.map_clip({1, 1, 2, 2}, clip) ||
        clip.left != 201 || clip.top != 1 || clip.right != 203 || clip.bottom != 3) return 7;
    float x = 0.0f, y = 0.0f;
    if (transform.inverse(0.0f, 450.0f, x, y)) return 8;
    if (transform.inverse_client_point(0.0f, 450.0f, 1600, 900, x, y) || x != 0.0f ||
        !(y >= 0.0f && y < 600.0f)) return 9;
    if (transform.inverse_client_point(1599.0f, 450.0f, 1600, 900, x, y) ||
        !(x >= 0.0f && x < 800.0f)) return 10;
    if (transform.inverse_client_point(1.0f, 1.0f, 0, 900, x, y)) return 11;
    if (sgl::ScreenTransform::create(0, 600, 1920, 1080, transform) ||
        sgl::ScreenTransform::create(800, 600, 0, 1080, transform)) return 12;

    for (const auto& size : std::array<std::array<unsigned, 4>, 2>{{
        {1024, 768, 320, 2239}, {1280, 1024, 380, 2179}}}) {
        if (!sgl::ScreenTransform::create(size[0], size[1], 2560, 1440, transform)) return 16;
        if (transform.inverse_client_point(static_cast<float>(size[2] - 1), 720.0f,
            2560, 1440, x, y) || x != 0.0f) return 17;
        if (!transform.inverse_client_point(static_cast<float>(size[2]), 720.0f,
            2560, 1440, x, y)) return 18;
        if (!transform.inverse_client_point(static_cast<float>(size[3]), 720.0f,
            2560, 1440, x, y)) return 19;
        if (transform.inverse_client_point(static_cast<float>(size[3] + 1), 720.0f,
            2560, 1440, x, y) || !(x >= 0.0f && x < size[0])) return 20;
    }

    if (!sgl::ScreenTransform::create_composed(800, 600, 1280, 1024, 2560, 1440, transform) ||
        std::abs(transform.scale - 2.25) > 0.000001 ||
        std::abs(transform.offset_x - 380.0) > 0.000001 ||
        std::abs(transform.offset_y - 45.0) > 0.000001) return 21;
    const auto options_plus = transform.map({720.0f, 120.0f, 740.0f, 140.0f});
    if (std::abs(options_plus.left - 2000.0f) > 0.0001f ||
        std::abs(options_plus.top - 315.0f) > 0.0001f ||
        std::abs(options_plus.right - 2045.0f) > 0.0001f ||
        std::abs(options_plus.bottom - 360.0f) > 0.0001f) return 22;
    if (!sgl::ScreenTransform::create_composed(800, 600, 1600, 900, 2560, 1440, transform) ||
        std::abs(transform.scale - 2.4) > 0.000001 ||
        std::abs(transform.offset_x - 320.0) > 0.000001 ||
        std::abs(transform.offset_y) > 0.000001) return 23;
    if (sgl::ScreenTransform::create_composed(800, 600, 0, 900, 2560, 1440, transform)) return 24;

    // Clips that share a logical edge must share the mapped edge: each output pixel belongs to exactly one of them.
    // The options menu splits its dirty regions at the pointer, e.g. [526,648) and [648,729) at a 2.4 scale.
    if (!sgl::ScreenTransform::create(800, 600, 2560, 1440, transform)) return 25;
    RECT first{}, second{};
    if (!transform.map_clip({526, 242, 648, 265}, first) || !transform.map_clip({648, 242, 729, 265}, second) ||
        first.right != second.left || first.right != 1875) return 26;
    for (LONG edge = 1; edge < 600; ++edge) {
        if (!transform.map_clip({0, 0, edge, edge}, first) || !transform.map_clip({edge, edge, 800, 600}, second) ||
            first.right != second.left || first.bottom != second.top) return 27;
        // The mapped clip holds exactly the pixels whose centers a quad over the logical range covers.
        const double mapped = transform.offset_x + edge * transform.scale;
        if (!(first.right - 0.5 < mapped + 1e-9 && first.right + 0.5 >= mapped - 1e-9)) return 28;
    }

    // The border bars and the mapped screen cover every output pixel exactly once: pillarbox (16:9), letterbox (5:4),
    // a fractional scale (1366x768) and no border at all (same aspect).
    struct BorderCase { unsigned width, height; std::size_t bars; };
    const std::array<BorderCase, 6> border_cases = {{
        {2560, 1440, 2}, {1920, 1080, 2}, {1600, 900, 2}, {1280, 1024, 2}, {1366, 768, 2}, {1600, 1200, 0}
    }};
    for (const auto& item : border_cases) {
        if (!sgl::ScreenTransform::create(800, 600, item.width, item.height, transform)) return 29;
        std::array<RECT, 4> bars{};
        const std::size_t count = transform.border_rectangles(bars);
        if (count != item.bars) return 30;
        RECT inner{};
        if (!transform.map_clip({0, 0, 800, 600}, inner)) return 31;
        std::vector<unsigned char> covered(static_cast<std::size_t>(item.width) * item.height);
        const auto cover = [&](const RECT& rect) {
            for (LONG y = rect.top; y < rect.bottom; ++y)
                for (LONG x = rect.left; x < rect.right; ++x) ++covered[static_cast<std::size_t>(y) * item.width + x];
        };
        cover(inner);
        for (std::size_t i = 0; i < count; ++i) {
            if (bars[i].left < 0 || bars[i].top < 0 || bars[i].right > static_cast<LONG>(item.width) ||
                bars[i].bottom > static_cast<LONG>(item.height) || bars[i].left >= bars[i].right ||
                bars[i].top >= bars[i].bottom) return 32;
            cover(bars[i]);
        }
        for (const unsigned char hits : covered) if (hits != 1) return 33;
    }
    if (!sgl::ScreenTransform::create(800, 600, 2560, 1440, transform)) return 34;
    std::array<RECT, 4> qhd_bars{};
    if (transform.border_rectangles(qhd_bars) != 2 ||
        qhd_bars[0].left != 0 || qhd_bars[0].top != 0 || qhd_bars[0].right != 320 || qhd_bars[0].bottom != 1440 ||
        qhd_bars[1].left != 2240 || qhd_bars[1].top != 0 || qhd_bars[1].right != 2560 || qhd_bars[1].bottom != 1440)
        return 35;
    return 0;
}
