#include "surface_renderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    constexpr std::uint32_t width = 3;
    constexpr std::uint32_t height = 2;
    constexpr std::uint32_t pitch = 12;
    std::vector<std::uint8_t> source_pixels(pitch * height, 0xe7);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto offset = static_cast<std::size_t>(y) * pitch + x * 3;
            source_pixels[offset] = static_cast<std::uint8_t>(10 + x + y * 3);
            source_pixels[offset + 1] = static_cast<std::uint8_t>(40 + x + y * 3);
            source_pixels[offset + 2] = static_cast<std::uint8_t>(70 + x + y * 3);
        }
    }
    sgl::DecodedImage source{width, height, pitch, sgl::ImageFormat::Bgr24, source_pixels, {}};
    sgl::DecodedImage bgra{width, height, width * 4, sgl::ImageFormat::Bgra32,
        std::vector<std::uint8_t>(width * height * 4, 0x44), {}};
    sgl::DecodedImage bgr_target{width, height, pitch, sgl::ImageFormat::Bgr24,
        std::vector<std::uint8_t>(pitch * height, 0xa5), {}};

    int source_id = 0, bgra_id = 0, bgr_target_id = 0;
    sgl::SurfaceRenderer renderer;
    // The expected pixels follow the original integer blend formulas.
    renderer.set_exact_blend(true);
    const RECT clip{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const sgl::Rect bounds{0, 0, static_cast<float>(width), static_cast<float>(height)};
    HRESULT result = renderer.draw(&source_id, source, &bgra_id, bgra, bounds, bounds, clip,
        std::nullopt, sgl::BlendMode::Opaque);
    if (FAILED(result)) return 1;
    if (bgra.pixels.front() != 0x44) {
        std::fprintf(stderr, "GPU draw unexpectedly changed the CPU copy before synchronization.\n");
        return 6;
    }
    result = renderer.synchronize_to_cpu(&bgra_id, bgra);
    if (FAILED(result)) return 7;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto source_offset = static_cast<std::size_t>(y) * pitch + x * 3;
            const auto target_offset = (static_cast<std::size_t>(y) * width + x) * 4;
            if (std::memcmp(source.pixels.data() + source_offset, bgra.pixels.data() + target_offset, 3) != 0 ||
                bgra.pixels[target_offset + 3] != 0xff) {
                std::fprintf(stderr, "BGR-to-BGRA mismatch at %u,%u.\n", x, y);
                return 2;
            }
        }
    }

    result = renderer.draw(&source_id, source, &bgr_target_id, bgr_target, bounds, bounds, clip,
        std::nullopt, sgl::BlendMode::Opaque);
    if (FAILED(result)) return 3;
    result = renderer.synchronize_to_cpu(&bgr_target_id, bgr_target);
    if (FAILED(result)) return 8;
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto row = static_cast<std::size_t>(y) * pitch;
        if (std::memcmp(source.pixels.data() + row, bgr_target.pixels.data() + row, width * 3) != 0) return 4;
        for (std::uint32_t x = width * 3; x < pitch; ++x) {
            if (bgr_target.pixels[row + x] != 0xa5) return 5;
        }
    }

    const sgl::DecodedImage changed_source{width + 1, height, pitch, sgl::ImageFormat::Bgr24, source_pixels, {}};
    result = renderer.draw(&source_id, changed_source, &bgra_id, bgra, bounds, bounds, clip,
        std::nullopt, sgl::BlendMode::Opaque);
    if (result != E_INVALIDARG) {
        std::fprintf(stderr, "A renderer surface was reused with different source dimensions.\n");
        return 15;
    }

    const auto preserved_offset = static_cast<std::size_t>(pitch) + 4;
    const std::uint8_t cpu_pixel[] = {0x21, 0x43, 0x65, 0xff};
    std::memcpy(bgra.pixels.data() + preserved_offset, cpu_pixel, sizeof(cpu_pixel));
    renderer.mark_cpu_modified(&bgra_id);
    const sgl::Rect first_pixel{0, 0, 1, 1};
    result = renderer.draw(&source_id, source, &bgra_id, bgra, first_pixel, first_pixel, clip,
        std::nullopt, sgl::BlendMode::Opaque);
    if (FAILED(result)) return 9;
    result = renderer.synchronize_to_cpu(&bgra_id, bgra);
    if (FAILED(result)) return 10;
    if (std::memcmp(bgra.pixels.data() + preserved_offset, cpu_pixel, sizeof(cpu_pixel)) != 0) {
        std::fprintf(stderr, "CPU modification was lost when the GPU surface was updated.\n");
        return 11;
    }

    int alpha_source_id = 0;
    const std::vector<std::uint8_t> alpha_pixels = {200, 100, 50, 128};
    const sgl::DecodedImage alpha_source{1, 1, 4, sgl::ImageFormat::Bgra32, alpha_pixels, {}};
    const sgl::Rect alpha_source_rect{0, 0, 1, 1};
    const auto alpha_destination_rect = alpha_source_rect;
    result = renderer.draw(&alpha_source_id, alpha_source, &bgra_id, bgra,
        alpha_source_rect, alpha_destination_rect, clip, std::nullopt, sgl::BlendMode::Alpha);
    if (FAILED(result)) return 12;
    result = renderer.synchronize_to_cpu(&bgra_id, bgra);
    if (FAILED(result)) return 13;
    const std::uint8_t expected_blend[] = {103, 69, 59, 191};
    if (std::memcmp(bgra.pixels.data(), expected_blend, sizeof(expected_blend)) != 0 ||
        std::memcmp(bgra.pixels.data() + preserved_offset, cpu_pixel, sizeof(cpu_pixel)) != 0) {
        std::fprintf(stderr, "A clipped alpha draw changed pixels outside its destination rectangle.\n");
        return 14;
    }

    // Queued blended draws must match drawing one at a time: a draw that reads pixels an earlier queued draw writes
    // ends the batch, while draws that do not overlap share one batch.
    const auto run_sequence = [](bool synchronize_each, std::vector<std::uint8_t>& output) -> HRESULT {
        constexpr std::uint32_t sequence_width = 8;
        sgl::DecodedImage target{sequence_width, 1, sequence_width * 4, sgl::ImageFormat::Bgra32, {}, {}};
        for (std::uint32_t x = 0; x < sequence_width; ++x) {
            const std::uint8_t pixel[] = {static_cast<std::uint8_t>(20 + x * 25), static_cast<std::uint8_t>(200 - x * 20),
                static_cast<std::uint8_t>(90 + x * 7), 0xff};
            target.pixels.insert(target.pixels.end(), pixel, pixel + 4);
        }
        const sgl::DecodedImage opaque{1, 1, 4, sgl::ImageFormat::Bgra32, {12, 230, 140, 0xff}, {}};
        const sgl::DecodedImage translucent{2, 1, 8, sgl::ImageFormat::Bgra32, {200, 100, 50, 128, 30, 250, 170, 77}, {}};
        int opaque_id = 0, translucent_id = 0, target_id = 0;
        sgl::SurfaceRenderer sequence_renderer;
        sequence_renderer.set_exact_blend(true);
        const RECT full{0, 0, static_cast<LONG>(sequence_width), 1};
        struct Step { const sgl::DecodedImage* source; int* id; float left; float width; sgl::BlendMode blend; };
        const Step steps[] = {
            {&opaque, &opaque_id, 2, 1, sgl::BlendMode::Opaque},
            {&translucent, &translucent_id, 2, 1, sgl::BlendMode::Alpha},
            {&translucent, &translucent_id, 5, 1, sgl::BlendMode::Alpha},
            {&translucent, &translucent_id, 0, 2, sgl::BlendMode::Alpha},
            {&translucent, &translucent_id, 5, 2, sgl::BlendMode::Multiply2},
            {&opaque, &opaque_id, 7, 1, sgl::BlendMode::Opaque},
            {&translucent, &translucent_id, 3, 1, sgl::BlendMode::Alpha},
        };
        for (const Step& step : steps) {
            const sgl::Rect source_rect{0, 0, step.width, 1};
            const sgl::Rect destination{step.left, 0, step.left + step.width, 1};
            HRESULT hr = sequence_renderer.draw(step.id, *step.source, &target_id, target, source_rect, destination, full,
                std::nullopt, step.blend);
            if (FAILED(hr)) return hr;
            if (synchronize_each) {
                hr = sequence_renderer.synchronize_to_cpu(&target_id, target);
                if (FAILED(hr)) return hr;
            }
        }
        const HRESULT hr = sequence_renderer.synchronize_to_cpu(&target_id, target);
        output = target.pixels;
        return hr;
    };
    std::vector<std::uint8_t> one_at_a_time, batched;
    if (FAILED(run_sequence(true, one_at_a_time)) || FAILED(run_sequence(false, batched))) return 16;
    if (one_at_a_time != batched) {
        std::fprintf(stderr, "Batched blended draws differ from drawing them one at a time.\n");
        return 17;
    }

    std::puts("GPU region copy preserved blended pixels and untouched destination content.");
    return 0;
}
