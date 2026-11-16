#pragma once

#include "compositor.hpp"

#include <algorithm>
#include <cmath>

namespace sgl {

struct ScreenTransform {
    unsigned logical_width = 0;
    unsigned logical_height = 0;
    unsigned output_width = 0;
    unsigned output_height = 0;
    double scale = 0.0;
    double offset_x = 0.0;
    double offset_y = 0.0;

    static bool create(unsigned logical_width, unsigned logical_height, unsigned output_width,
        unsigned output_height, ScreenTransform& result) noexcept {
        if (!logical_width || !logical_height || !output_width || !output_height) return false;
        const double scale = std::min(static_cast<double>(output_width) / logical_width,
            static_cast<double>(output_height) / logical_height);
        if (!std::isfinite(scale) || scale <= 0.0) return false;
        result = {logical_width, logical_height, output_width, output_height, scale,
            (output_width - logical_width * scale) / 2.0,
            (output_height - logical_height * scale) / 2.0};
        return true;
    }

    static bool create_composed(unsigned logical_width, unsigned logical_height,
        unsigned intermediate_width, unsigned intermediate_height, unsigned output_width,
        unsigned output_height, ScreenTransform& result) noexcept {
        ScreenTransform to_intermediate;
        ScreenTransform to_output;
        if (!create(logical_width, logical_height, intermediate_width, intermediate_height, to_intermediate) ||
            !create(intermediate_width, intermediate_height, output_width, output_height, to_output)) return false;
        const double scale = to_intermediate.scale * to_output.scale;
        if (!std::isfinite(scale) || scale <= 0.0) return false;
        result = {logical_width, logical_height, output_width, output_height, scale,
            to_output.offset_x + to_intermediate.offset_x * to_output.scale,
            to_output.offset_y + to_intermediate.offset_y * to_output.scale};
        return true;
    }

    Rect map(Rect rect) const noexcept {
        return {
            static_cast<float>(offset_x + rect.left * scale),
            static_cast<float>(offset_y + rect.top * scale),
            static_cast<float>(offset_x + rect.right * scale),
            static_cast<float>(offset_y + rect.bottom * scale)
        };
    }

    bool map_clip(const RECT& clip, RECT& result) const noexcept {
        const double left = offset_x + clip.left * scale;
        const double top = offset_y + clip.top * scale;
        const double right = offset_x + clip.right * scale;
        const double bottom = offset_y + clip.bottom * scale;
        result = {
            clamp_pixel(std::floor(left), output_width),
            clamp_pixel(std::floor(top), output_height),
            clamp_pixel(std::ceil(right), output_width),
            clamp_pixel(std::ceil(bottom), output_height)
        };
        return result.left < result.right && result.top < result.bottom;
    }

    bool inverse(float output_x, float output_y, float& logical_x, float& logical_y) const noexcept {
        const double x = (output_x - offset_x) / scale;
        const double y = (output_y - offset_y) / scale;
        if (x < 0.0 || y < 0.0 || x >= logical_width || y >= logical_height) return false;
        logical_x = static_cast<float>(x);
        logical_y = static_cast<float>(y);
        return true;
    }

    // Coordinates stay clamped for drag/release handling; the result reports whether the point is drawable.
    bool inverse_client_point(float client_x, float client_y, unsigned client_width, unsigned client_height,
        float& logical_x, float& logical_y) const noexcept {
        if (!logical_width || !logical_height || !output_width || !output_height || scale <= 0.0 ||
            !client_width || !client_height || !std::isfinite(client_x) || !std::isfinite(client_y)) return false;

        const double output_x = (client_x + 0.5) * output_width / client_width;
        const double output_y = (client_y + 0.5) * output_height / client_height;
        const bool inside = output_x >= offset_x && output_x < offset_x + logical_width * scale &&
            output_y >= offset_y && output_y < offset_y + logical_height * scale;
        const double logical_unclamped_x = (output_x - offset_x) / scale;
        const double logical_unclamped_y = (output_y - offset_y) / scale;
        const double x = std::clamp(logical_unclamped_x, 0.0, static_cast<double>(logical_width));
        const double y = std::clamp(logical_unclamped_y, 0.0, static_cast<double>(logical_height));
        logical_x = static_cast<float>(x);
        logical_y = static_cast<float>(y);
        if (logical_x >= logical_width) logical_x = std::nextafter(static_cast<float>(logical_width), 0.0f);
        if (logical_y >= logical_height) logical_y = std::nextafter(static_cast<float>(logical_height), 0.0f);
        return inside;
    }

private:
    static LONG clamp_pixel(double coordinate, unsigned extent) noexcept {
        return static_cast<LONG>(std::clamp(coordinate, 0.0, static_cast<double>(extent)));
    }
};

}
