#include "screen_transform.hpp"

#include <array>
#include <cmath>

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
    return 0;
}
