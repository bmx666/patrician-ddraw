#include "compositor.hpp"

#include <d3dcompiler.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iterator>

using Microsoft::WRL::ComPtr;

namespace {
// Color key value for draws without a key. The game keys on magenta, so the value never collides with a real key.
constexpr std::uint32_t no_color_key = 0xffffffffu;

constexpr char shader[] = R"(
// Per-draw data, one instance per draw (the Instance layout below).
struct Instance {
    float4 destination : DESTINATION;
    float4 source_uv : SOURCE_UV;
    float4 sizes : SIZES; // output size, source origin
    uint4 sampling : SAMPLING; // color key, fixed sampling, fixed origin
    uint4 steps : STEPS; // fixed step, fractional step as float bits
    float4 modulation : MODULATION;
    int4 clip : CLIP;
};
struct Vertex {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation float4 destination : DESTINATION;
    nointerpolation float4 source_uv : SOURCE_UV;
    nointerpolation float4 steps : STEPS; // source origin, fractional step
    nointerpolation uint4 sampling : SAMPLING;
    nointerpolation uint2 fixed_step : FIXED_STEP;
    nointerpolation float4 modulation : MODULATION;
};
// The draw's values for the pixel shader functions, set by load_draw at the start of every entry point.
static float4 destination;
static float4 source_uv;
static float2 source_origin;
static uint color_key;
static uint fixed_sampling;
static uint2 fixed_origin;
static uint2 fixed_step;
static float2 fractional_step;
static float4 modulation_color;
void load_draw(Vertex input) {
    destination = input.destination;
    source_uv = input.source_uv;
    source_origin = input.steps.xy;
    fractional_step = input.steps.zw;
    color_key = input.sampling.x;
    fixed_sampling = input.sampling.y;
    fixed_origin = input.sampling.zw;
    fixed_step = input.fixed_step;
    modulation_color = input.modulation;
}
Vertex vertex_main(uint id : SV_VertexID, Instance draw) {
    static const float2 corners[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    float2 corner = corners[id];
    // The quad is cut to the clip rectangle instead of a per-draw scissor. Its integral edges cover the same pixel
    // centers, and an edge inside the clip keeps exactly the unclipped position and texture coordinate.
    float4 area = float4(max(draw.destination.xy, float2(draw.clip.xy)), min(draw.destination.zw, float2(draw.clip.zw)));
    area.zw = max(area.zw, area.xy);
    float2 extent = draw.destination.zw - draw.destination.xy;
    float2 start = area.xy == draw.destination.xy ? float2(0, 0) : (area.xy - draw.destination.xy) / extent;
    float2 end = area.zw == draw.destination.zw ? float2(1, 1) : (area.zw - draw.destination.xy) / extent;
    float2 along = corner > 0.5 ? end : start;
    float2 pixel = lerp(area.xy, area.zw, corner);
    Vertex result;
    result.position = float4(pixel.x / draw.sizes.x * 2 - 1,
                             1 - pixel.y / draw.sizes.y * 2, 0, 1);
    result.uv = lerp(draw.source_uv.xy, draw.source_uv.zw, along);
    result.destination = draw.destination;
    result.source_uv = draw.source_uv;
    result.steps = float4(draw.sizes.zw, asfloat(draw.steps.zw));
    result.sampling = draw.sampling;
    result.fixed_step = draw.steps.xy;
    result.modulation = draw.modulation;
    return result;
}
Texture2D source_texture : register(t0);
Texture2D<uint> index_texture : register(t1);
Texture2D palette_texture : register(t2);
Texture2D background_texture : register(t3);
Texture2D<uint> alpha_mask_texture : register(t4);
SamplerState source_sampler : register(s0);
SamplerState present_sampler : register(s1);
float4 modulate_color(float4 color) {
    return color * modulation_color;
}
float4 sample_pixel(Vertex input) {
    if (fixed_sampling == 3) {
        uint2 position = fixed_origin + uint2(floor(input.position.xy) - source_origin) * fixed_step;
        uint2 pixel = position >> 15;
        int2 weight = int2(position & 32767);
        uint width, height;
        source_texture.GetDimensions(width, height);
        uint2 adjacent = min(pixel + 1, uint2(width - 1, height - 1));
        int4 a = int4(round(source_texture.Load(int3(pixel, 0)) * 255));
        int4 b = int4(round(source_texture.Load(int3(pixel.x, adjacent.y, 0)) * 255));
        int4 c = int4(round(source_texture.Load(int3(adjacent.x, pixel.y, 0)) * 255));
        int4 d = int4(round(source_texture.Load(int3(adjacent, 0)) * 255));
        // Match the MMX filter's intermediate truncation before multiplication.
        int4 left = (a << 6) + (((b - a) * weight.y) >> 9);
        int4 right = (c << 6) + (((d - c) * weight.y) >> 9);
        return float4(((left >> 1) + (((right - left) * weight.x) >> 16)) >> 5) / 255.0;
    }
    if (fixed_sampling == 1) {
        uint2 pixel = fixed_origin + ((uint2(floor(input.position.xy) - destination.xy) * fixed_step + (fixed_step >> 1)) >> 16);
        return source_texture.Load(int3(pixel, 0));
    }
    if (fixed_sampling == 2) {
        float2 pixel_center = floor(input.position.xy) + 0.5;
        float2 relative = max(pixel_center - destination.xy, 0.0);
        uint2 pixel = uint2(floor(source_origin + relative * fractional_step));
        return source_texture.Load(int3(pixel, 0));
    }
    if (fixed_sampling == 4) {
        uint width, height;
        source_texture.GetDimensions(width, height);
        float2 size = float2(width, height);
        float2 texel = input.uv * size;
        float2 seam = floor(texel + 0.5);
        texel = seam + clamp((texel - seam) / max(fwidth(texel), 1e-5), -0.5, 0.5);
        // Never blend in texels outside the source rectangle.
        texel = clamp(texel, source_uv.xy * size + 0.5, source_uv.zw * size - 0.5);
        return source_texture.SampleLevel(present_sampler, texel / size, 0);
    }
    return source_texture.Sample(source_sampler, input.uv);
}
float4 pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    return modulate_color(sample_pixel(input));
}
bool matches_key(float4 color) {
    uint4 channels = uint4(round(saturate(color) * 255));
    uint packed = (channels.a << 24) | (channels.r << 16) | (channels.g << 8) | channels.b;
    return packed == (color_key & 0xffffff);
}
float4 keyed_pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    float4 color = sample_pixel(input);
    if (matches_key(color)) discard;
    return modulate_color(color);
}
float4 alpha_blend(float4 color, Vertex input) {
    // The original scales every channel by the constant color with (source * constant) >> 8, which is also the
    // (source * 255) >> 8 it applies for opaque white.
    int4 source = int4(round(saturate(color) * 255));
    int4 tint = int4(round(saturate(modulation_color) * 255));
    int4 background = int4(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0))) * 255));
    source = (source * tint) >> 8;
    int4 product = (source - background) * source.a;
    product = (product << 16) >> 16;
    return float4((background + (product >> 8)) & 255) / 255.0;
}
float4 alpha_pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    return alpha_blend(sample_pixel(input), input);
}
float4 additive_blend(float4 color, Vertex input) {
    int4 source = int4(round(saturate(color) * 255));
    int4 background = int4(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0))) * 255));
    return float4(min(source.rgb + background.rgb, int3(255, 255, 255)), 255) / 255.0;
}
float4 additive_pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    return additive_blend(sample_pixel(input), input);
}
float4 additive_modulated_blend(float4 color, Vertex input) {
    int3 source = int3(round(saturate(color.rgb) * 255));
    int3 tint = int3(round(saturate(modulation_color.rgb) * 255));
    int3 background = int3(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0)).rgb) * 255));
    source = (source * tint) >> 8;
    return float4(min(source + background, int3(255, 255, 255)), 255) / 255.0;
}
float4 additive_modulated_pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    return additive_modulated_blend(sample_pixel(input), input);
}
float4 multiply_blend(float4 color, Vertex input) {
    int4 source = int4(round(saturate(color) * 255));
    int4 background = int4(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0))) * 255));
    return float4((source * background) >> 8) / 255.0;
}
float4 multiply_pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    return multiply_blend(sample_pixel(input), input);
}
float4 multiply_modulated_blend(float4 color, Vertex input) {
    int4 source = int4(round(saturate(color) * 255));
    int4 tint = int4(round(saturate(modulation_color) * 255));
    int4 background = int4(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0))) * 255));
    source = (source * tint) >> 8;
    return float4((source * background) >> 8) / 255.0;
}
float4 multiply_modulated_pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    return multiply_modulated_blend(sample_pixel(input), input);
}
float4 multiply2_pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    // The original doubled multiply ignores the constant color.
    int4 source = int4(round(saturate(sample_pixel(input)) * 255));
    int4 background = int4(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0))) * 255));
    return float4(min((source * background * 2) >> 8, 255)) / 255.0;
}
float4 fill_alpha_pixel_main(Vertex input) : SV_Target {
    load_draw(input);
    int3 source = int3(round(saturate(sample_pixel(input).rgb) * 255));
    int alpha = int(round(saturate(modulation_color.a) * 255));
    int3 background = int3(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0)).rgb) * 255));
    return float4(((source * alpha) >> 8) + ((background * (255 - alpha)) >> 8), 255) / 255.0;
}
uint2 indexed_pixel(Vertex input) {
    uint width, height;
    index_texture.GetDimensions(width, height);
    uint2 pixel = uint2(input.uv * float2(width, height));
    if (fixed_sampling == 1)
        pixel = fixed_origin + ((uint2(floor(input.position.xy) - destination.xy) * fixed_step + (fixed_step >> 1)) >> 16);
    else if (fixed_sampling == 2)
        pixel = uint2(floor(source_origin + max(floor(input.position.xy) + 0.5 - destination.xy, 0.0) * fractional_step));
    return min(pixel, uint2(width - 1, height - 1));
}
float4 sample_indexed(Vertex input) {
    uint2 pixel = indexed_pixel(input);
    uint index = index_texture.Load(int3(pixel, 0));
    return palette_texture.Load(int3(index, 0, 0));
}
float4 indexed_main(Vertex input) : SV_Target {
    load_draw(input);
    return modulate_color(sample_indexed(input));
}
float4 keyed_indexed_main(Vertex input) : SV_Target {
    load_draw(input);
    float4 color = sample_indexed(input);
    if (matches_key(color)) discard;
    return modulate_color(color);
}
float4 alpha_indexed_main(Vertex input) : SV_Target {
    load_draw(input);
    float4 color = sample_indexed(input);
    // Palette entries with the color key's RGB stay transparent whatever their alpha.
    uint3 channels = uint3(round(saturate(color.rgb) * 255));
    if (color_key != 0xffffffff && ((channels.r << 16) | (channels.g << 8) | channels.b) == (color_key & 0xffffff))
        discard;
    return alpha_blend(color, input);
}
float4 multiply_indexed_main(Vertex input) : SV_Target {
    load_draw(input);
    int4 source = int4(round(saturate(sample_indexed(input)) * 255));
    int4 background = int4(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0))) * 255));
    return float4((source * background) >> 8) / 255.0;
}
float4 multiply_modulated_indexed_main(Vertex input) : SV_Target {
    load_draw(input);
    int4 source = int4(round(saturate(sample_indexed(input)) * 255));
    int4 tint = int4(round(saturate(modulation_color) * 255));
    int4 background = int4(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0))) * 255));
    source = (source * tint) >> 8;
    return float4((source * background) >> 8) / 255.0;
}
float4 additive_indexed_main(Vertex input) : SV_Target {
    load_draw(input);
    return additive_blend(sample_indexed(input), input);
}
float4 additive_modulated_indexed_main(Vertex input) : SV_Target {
    load_draw(input);
    return additive_modulated_blend(sample_indexed(input), input);
}
float4 alpha_mask_blend(float4 color, uint mask_alpha, Vertex input) {
    int4 source = int4(round(saturate(color) * 255));
    int4 destination = int4(round(saturate(background_texture.Load(int3(uint2(input.position.xy), 0))) * 255));
    int4 tint = int4(round(saturate(modulation_color) * 255));
    int opacity = (int(mask_alpha) * tint.a) >> 8;
    source.rgb = (source.rgb * tint.rgb) >> 8;
    int4 output_color;
    output_color.rgb = (source.rgb * opacity + destination.rgb * (256 - opacity)) >> 8;
    output_color.a = 255 - ((opacity * (255 - opacity) + 255) >> 8);
    return float4(output_color) / 255.0;
}
float4 alpha_masked_indexed_main(Vertex input) : SV_Target {
    load_draw(input);
    uint2 pixel = indexed_pixel(input);
    uint mask_alpha = alpha_mask_texture.Load(int3(pixel, 0));
    if (mask_alpha == 0) discard;
    uint index = index_texture.Load(int3(pixel, 0));
    return alpha_mask_blend(palette_texture.Load(int3(index, 0, 0)), mask_alpha, input);
}
// Source colors for the hardware blend states, which combine them with the target in place of a background copy.
int4 tinted(float4 color) {
    return (int4(round(saturate(color) * 255)) * int4(round(saturate(modulation_color) * 255))) >> 8;
}
float4 plain_source_main(Vertex input) : SV_Target {
    load_draw(input);
    return sample_pixel(input);
}
float4 plain_indexed_source_main(Vertex input) : SV_Target {
    load_draw(input);
    return sample_indexed(input);
}
float4 tinted_source_main(Vertex input) : SV_Target {
    load_draw(input);
    return float4(tinted(sample_pixel(input))) / 255.0;
}
float4 tinted_indexed_source_main(Vertex input) : SV_Target {
    load_draw(input);
    return float4(tinted(sample_indexed(input))) / 255.0;
}
float4 keyed_tinted_indexed_source_main(Vertex input) : SV_Target {
    load_draw(input);
    float4 color = sample_indexed(input);
    uint3 channels = uint3(round(saturate(color.rgb) * 255));
    if (color_key != 0xffffffff && ((channels.r << 16) | (channels.g << 8) | channels.b) == (color_key & 0xffffff))
        discard;
    return float4(tinted(color)) / 255.0;
}
float4 rgb_source_main(Vertex input) : SV_Target {
    load_draw(input);
    return float4(sample_pixel(input).rgb, 1);
}
float4 rgb_indexed_source_main(Vertex input) : SV_Target {
    load_draw(input);
    return float4(sample_indexed(input).rgb, 1);
}
float4 tinted_rgb_source_main(Vertex input) : SV_Target {
    load_draw(input);
    return float4(float3(tinted(sample_pixel(input)).rgb) / 255.0, 1);
}
float4 tinted_rgb_indexed_source_main(Vertex input) : SV_Target {
    load_draw(input);
    return float4(float3(tinted(sample_indexed(input)).rgb) / 255.0, 1);
}
float4 masked_source_main(Vertex input) : SV_Target {
    load_draw(input);
    uint2 pixel = indexed_pixel(input);
    uint mask_alpha = alpha_mask_texture.Load(int3(pixel, 0));
    if (mask_alpha == 0) discard;
    uint index = index_texture.Load(int3(pixel, 0));
    int4 tint = int4(round(saturate(modulation_color) * 255));
    int3 source = (int3(round(saturate(palette_texture.Load(int3(index, 0, 0)).rgb) * 255)) * tint.rgb) >> 8;
    return float4(float3(source) / 255.0, float((int(mask_alpha) * tint.a) >> 8) / 255.0);
}
)";

// Entry points of the hardware blend sources, in Compositor::BlendSource order.
constexpr const char* blend_source_entries[] = {
    "plain_source_main", "plain_indexed_source_main", "tinted_source_main", "tinted_indexed_source_main",
    "keyed_tinted_indexed_source_main", "rgb_source_main", "rgb_indexed_source_main", "tinted_rgb_source_main",
    "tinted_rgb_indexed_source_main", "masked_source_main"};

struct alignas(16) Geometry {
    float destination[4];
    float source_uv[4];
    float output_size[2];
    float source_origin[2];
    std::uint32_t color_key;
    std::uint32_t fixed_sampling;
    std::uint32_t fixed_origin[2];
    std::uint32_t fixed_step[2];
    float fractional_step[2];
    float modulation_color[4];
};

static_assert(sizeof(Geometry) == 96);

// One draw's vertex buffer entry: its Geometry followed by the clip rectangle (the shader's Instance).
struct alignas(16) Instance {
    Geometry geometry;
    std::int32_t clip[4];
};

static_assert(sizeof(Instance) == 112);

constexpr D3D11_INPUT_ELEMENT_DESC instance_elements[] = {
    {"DESTINATION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"SOURCE_UV", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"SIZES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"SAMPLING", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"STEPS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"MODULATION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"CLIP", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
};

bool same_state(const sgl::Compositor::PreparedDraw& a, const sgl::Compositor::PreparedDraw& b) noexcept {
    return a.target == b.target && a.target_width == b.target_width && a.target_height == b.target_height &&
        a.pixel_shader == b.pixel_shader && a.blend_state == b.blend_state && a.blend_factor == b.blend_factor &&
        std::memcmp(a.resources, b.resources, sizeof(a.resources)) == 0;
}


bool valid_rect(sgl::Rect rect) noexcept {
    return std::isfinite(rect.left) && std::isfinite(rect.top) &&
        std::isfinite(rect.right) && std::isfinite(rect.bottom) &&
        rect.left < rect.right && rect.top < rect.bottom;
}

}

namespace sgl {

// D3DCompile is resolved at run time instead of imported: a missing d3dcompiler_47.dll (some Wine/Proton
// prefixes) would otherwise stop the game from loading this library at all. Any of these versions compiles
// the shaders below (shader model 4.0).
static pD3DCompile d3d_compile_function() noexcept {
    static const pD3DCompile function = [] {
        for (const wchar_t* name : {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll"}) {
            const HMODULE module = LoadLibraryW(name);
            if (!module) continue;
            if (const auto found = reinterpret_cast<pD3DCompile>(GetProcAddress(module, "D3DCompile"))) return found;
            FreeLibrary(module);
        }
        return pD3DCompile{};
    }();
    return function;
}

// Render target blend of a hardware blend state. The fixed-function equations stand in for the integer formulas of the
// background-copy shaders and may differ from them by one step.
D3D11_RENDER_TARGET_BLEND_DESC Compositor::blend_desc(HardwareBlend mode) noexcept {
    D3D11_RENDER_TARGET_BLEND_DESC desc = {};
    desc.BlendEnable = TRUE;
    desc.BlendOp = desc.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    switch (mode) {
    case alpha_blend: // source and destination mixed by the source alpha in all four channels
        desc.SrcBlend = desc.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        desc.DestBlend = desc.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        break;
    case additive_blend: // saturated sum, alpha set by the source's 1
        desc.SrcBlend = desc.DestBlend = desc.SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.DestBlendAlpha = D3D11_BLEND_ZERO;
        break;
    case multiply_blend: // source times destination in all four channels
        desc.SrcBlend = D3D11_BLEND_DEST_COLOR;
        desc.SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
        desc.DestBlend = desc.DestBlendAlpha = D3D11_BLEND_ZERO;
        break;
    case multiply2_blend: // source * destination + destination * source, saturated
        desc.SrcBlend = D3D11_BLEND_DEST_COLOR;
        desc.DestBlend = D3D11_BLEND_SRC_COLOR;
        desc.SrcBlendAlpha = D3D11_BLEND_DEST_ALPHA;
        desc.DestBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        break;
    case fill_alpha_blend: // color mixed by the draw's constant alpha (the blend factor), alpha set by the source's 1
        desc.SrcBlend = D3D11_BLEND_BLEND_FACTOR;
        desc.DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
        desc.SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.DestBlendAlpha = D3D11_BLEND_ZERO;
        break;
    case alpha_mask_blend:
    default: // color mixed by the mask opacity; the target alpha is kept
        desc.SrcBlend = desc.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        desc.DestBlend = desc.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
            D3D11_COLOR_WRITE_ENABLE_BLUE;
        break;
    }
    return desc;
}

HRESULT Compositor::initialize(ID3D11Device* device) noexcept {
    renderer_state_context_ = nullptr;
    renderer_pixel_shader_ = nullptr;
    if (!device) return E_INVALIDARG;
    const pD3DCompile compile = d3d_compile_function();
    if (!compile) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    ComPtr<ID3DBlob> vertex_code;
    ComPtr<ID3DBlob> pixel_code;
    ComPtr<ID3DBlob> keyed_pixel_code;
    ComPtr<ID3DBlob> alpha_pixel_code;
    ComPtr<ID3DBlob> multiply_pixel_code;
    ComPtr<ID3DBlob> multiply_modulated_pixel_code;
    ComPtr<ID3DBlob> multiply2_pixel_code;
    ComPtr<ID3DBlob> fill_alpha_pixel_code;
    ComPtr<ID3DBlob> additive_pixel_code;
    ComPtr<ID3DBlob> additive_modulated_pixel_code;
    ComPtr<ID3DBlob> indexed_code;
    ComPtr<ID3DBlob> keyed_indexed_code;
    ComPtr<ID3DBlob> alpha_indexed_code;
    ComPtr<ID3DBlob> multiply_indexed_code;
    ComPtr<ID3DBlob> multiply_modulated_indexed_code;
    ComPtr<ID3DBlob> additive_indexed_code;
    ComPtr<ID3DBlob> additive_modulated_indexed_code;
    ComPtr<ID3DBlob> alpha_masked_indexed_code;
    ComPtr<ID3D11VertexShader> vertex_shader;
    ComPtr<ID3D11PixelShader> pixel_shader;
    ComPtr<ID3D11PixelShader> keyed_pixel_shader;
    ComPtr<ID3D11PixelShader> alpha_pixel_shader;
    ComPtr<ID3D11PixelShader> multiply_pixel_shader;
    ComPtr<ID3D11PixelShader> multiply_modulated_pixel_shader;
    ComPtr<ID3D11PixelShader> multiply2_pixel_shader;
    ComPtr<ID3D11PixelShader> fill_alpha_pixel_shader;
    ComPtr<ID3D11PixelShader> additive_pixel_shader;
    ComPtr<ID3D11PixelShader> additive_modulated_pixel_shader;
    ComPtr<ID3D11PixelShader> indexed_shader;
    ComPtr<ID3D11PixelShader> keyed_indexed_shader;
    ComPtr<ID3D11PixelShader> alpha_indexed_shader;
    ComPtr<ID3D11PixelShader> multiply_indexed_shader;
    ComPtr<ID3D11PixelShader> multiply_modulated_indexed_shader;
    ComPtr<ID3D11PixelShader> additive_indexed_shader;
    ComPtr<ID3D11PixelShader> additive_modulated_indexed_shader;
    ComPtr<ID3D11PixelShader> alpha_masked_indexed_shader;
    ComPtr<ID3D11Buffer> instances;
    ComPtr<ID3D11InputLayout> input_layout;
    ComPtr<ID3D11SamplerState> sampler_state;
    ComPtr<ID3D11SamplerState> present_sampler_state;
    ComPtr<ID3D11RasterizerState> rasterizer_state;
    ComPtr<ID3D11PixelShader> blend_source_shaders[blend_source_count];
    ComPtr<ID3D11BlendState> blend_states[hardware_blend_count];
    static_assert(std::size(blend_source_entries) == blend_source_count);
    HRESULT result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "vertex_main", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vertex_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "keyed_pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &keyed_pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "alpha_pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &alpha_pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "multiply_pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &multiply_pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "multiply_modulated_pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &multiply_modulated_pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "multiply2_pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &multiply2_pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "fill_alpha_pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &fill_alpha_pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "additive_pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &additive_pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "additive_modulated_pixel_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &additive_modulated_pixel_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "indexed_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &indexed_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "keyed_indexed_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &keyed_indexed_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "alpha_indexed_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &alpha_indexed_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "multiply_indexed_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &multiply_indexed_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "multiply_modulated_indexed_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &multiply_modulated_indexed_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "additive_indexed_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &additive_indexed_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "additive_modulated_indexed_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &additive_modulated_indexed_code, nullptr);
    if (FAILED(result)) return result;
    result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
        "alpha_masked_indexed_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &alpha_masked_indexed_code, nullptr);
    if (FAILED(result)) return result;
    result = device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(),
        nullptr, &vertex_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(indexed_code->GetBufferPointer(), indexed_code->GetBufferSize(),
        nullptr, &indexed_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(keyed_indexed_code->GetBufferPointer(), keyed_indexed_code->GetBufferSize(),
        nullptr, &keyed_indexed_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(alpha_indexed_code->GetBufferPointer(), alpha_indexed_code->GetBufferSize(),
        nullptr, &alpha_indexed_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(multiply_indexed_code->GetBufferPointer(), multiply_indexed_code->GetBufferSize(),
        nullptr, &multiply_indexed_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(multiply_modulated_indexed_code->GetBufferPointer(), multiply_modulated_indexed_code->GetBufferSize(),
        nullptr, &multiply_modulated_indexed_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(additive_indexed_code->GetBufferPointer(), additive_indexed_code->GetBufferSize(),
        nullptr, &additive_indexed_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(additive_modulated_indexed_code->GetBufferPointer(), additive_modulated_indexed_code->GetBufferSize(),
        nullptr, &additive_modulated_indexed_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(alpha_masked_indexed_code->GetBufferPointer(), alpha_masked_indexed_code->GetBufferSize(),
        nullptr, &alpha_masked_indexed_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(),
        nullptr, &pixel_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(keyed_pixel_code->GetBufferPointer(), keyed_pixel_code->GetBufferSize(),
        nullptr, &keyed_pixel_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(alpha_pixel_code->GetBufferPointer(), alpha_pixel_code->GetBufferSize(),
        nullptr, &alpha_pixel_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(multiply_pixel_code->GetBufferPointer(), multiply_pixel_code->GetBufferSize(),
        nullptr, &multiply_pixel_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(multiply_modulated_pixel_code->GetBufferPointer(), multiply_modulated_pixel_code->GetBufferSize(),
        nullptr, &multiply_modulated_pixel_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(multiply2_pixel_code->GetBufferPointer(), multiply2_pixel_code->GetBufferSize(),
        nullptr, &multiply2_pixel_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(fill_alpha_pixel_code->GetBufferPointer(), fill_alpha_pixel_code->GetBufferSize(),
        nullptr, &fill_alpha_pixel_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(additive_pixel_code->GetBufferPointer(), additive_pixel_code->GetBufferSize(),
        nullptr, &additive_pixel_shader);
    if (FAILED(result)) return result;
    result = device->CreatePixelShader(additive_modulated_pixel_code->GetBufferPointer(), additive_modulated_pixel_code->GetBufferSize(),
        nullptr, &additive_modulated_pixel_shader);
    if (FAILED(result)) return result;
    for (unsigned index = 0; index < blend_source_count; ++index) {
        ComPtr<ID3DBlob> code;
        result = compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
            blend_source_entries[index], "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, nullptr);
        if (FAILED(result)) return result;
        result = device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
            &blend_source_shaders[index]);
        if (FAILED(result)) return result;
    }
    for (unsigned index = 0; index < hardware_blend_count; ++index) {
        D3D11_BLEND_DESC blend = {};
        blend.RenderTarget[0] = blend_desc(static_cast<HardwareBlend>(index));
        result = device->CreateBlendState(&blend, &blend_states[index]);
        if (FAILED(result)) return result;
    }
    // A dynamic vertex buffer is written by mapping, so no copy command lands between two draws. A copy such as
    // UpdateSubresource would end the render pass on a tile-based GPU (Apple Silicon through D3DMetal) every draw.
    D3D11_BUFFER_DESC buffer = {};
    buffer.ByteWidth = sizeof(Instance) * instance_capacity;
    buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = device->CreateBuffer(&buffer, nullptr, &instances);
    if (FAILED(result)) return result;
    result = device->CreateInputLayout(instance_elements, static_cast<UINT>(std::size(instance_elements)),
        vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(), &input_layout);
    if (FAILED(result)) return result;
    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    result = device->CreateSamplerState(&sampler, &sampler_state);
    if (FAILED(result)) return result;
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    result = device->CreateSamplerState(&sampler, &present_sampler_state);
    if (FAILED(result)) return result;
    D3D11_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    // The vertex shader cuts each quad to its clip rectangle, so draws need no scissor of their own.
    rasterizer.ScissorEnable = FALSE;
    result = device->CreateRasterizerState(&rasterizer, &rasterizer_state);
    if (FAILED(result)) return result;
    device_ = device;
    vertex_shader_ = vertex_shader;
    pixel_shader_ = pixel_shader;
    keyed_pixel_shader_ = keyed_pixel_shader;
    alpha_pixel_shader_ = alpha_pixel_shader;
    multiply_pixel_shader_ = multiply_pixel_shader;
    multiply_modulated_pixel_shader_ = multiply_modulated_pixel_shader;
    multiply2_pixel_shader_ = multiply2_pixel_shader;
    fill_alpha_pixel_shader_ = fill_alpha_pixel_shader;
    additive_pixel_shader_ = additive_pixel_shader;
    additive_modulated_pixel_shader_ = additive_modulated_pixel_shader;
    indexed_shader_ = indexed_shader;
    keyed_indexed_shader_ = keyed_indexed_shader;
    alpha_indexed_shader_ = alpha_indexed_shader;
    multiply_indexed_shader_ = multiply_indexed_shader;
    multiply_modulated_indexed_shader_ = multiply_modulated_indexed_shader;
    additive_indexed_shader_ = additive_indexed_shader;
    additive_modulated_indexed_shader_ = additive_modulated_indexed_shader;
    alpha_masked_indexed_shader_ = alpha_masked_indexed_shader;
    instances_ = instances;
    input_layout_ = input_layout;
    instance_position_ = instance_capacity;
    sampler_ = sampler_state;
    present_sampler_ = present_sampler_state;
    rasterizer_ = rasterizer_state;
    std::copy(std::begin(blend_source_shaders), std::end(blend_source_shaders), std::begin(blend_source_shaders_));
    std::copy(std::begin(blend_states), std::end(blend_states), std::begin(blend_states_));
    return S_OK;
}

HRESULT Compositor::copy(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
    ID3D11ShaderResourceView* source, UINT target_width, UINT target_height,
    Rect source_rect, Rect destination_rect, RECT clip, Sampling sampling, std::optional<std::uint32_t> color_key,
    ID3D11ShaderResourceView* background, const char** failure_stage, std::uint32_t modulation_color,
    BlendMode blend) noexcept {
    return copy_impl(context, target, source, nullptr, nullptr, target_width, target_height, source_rect, destination_rect, clip,
        color_key, sampling, background, modulation_color, blend, failure_stage, nullptr);
}

HRESULT Compositor::copy_indexed(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
    ID3D11ShaderResourceView* indices, ID3D11ShaderResourceView* palette,
    UINT target_width, UINT target_height, Rect source_rect, Rect destination_rect, RECT clip,
    std::optional<std::uint32_t> color_key, Sampling sampling, const char** failure_stage,
    std::uint32_t modulation_color, ID3D11ShaderResourceView* background, BlendMode blend) noexcept {
    if (!palette) {
        if (failure_stage) *failure_stage = "compositor palette argument";
        return E_INVALIDARG;
    }
    return copy_impl(context, target, indices, palette, nullptr, target_width, target_height, source_rect, destination_rect, clip,
        color_key, sampling, background, modulation_color, blend, failure_stage, nullptr);
}

HRESULT Compositor::copy_masked_indexed(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
    ID3D11ShaderResourceView* indices, ID3D11ShaderResourceView* palette,
    ID3D11ShaderResourceView* mask, ID3D11ShaderResourceView* background,
    UINT target_width, UINT target_height, Rect source_rect, Rect destination_rect, RECT clip,
    std::uint32_t modulation_color, const char** failure_stage) noexcept {
    if (!palette || !mask || !background) {
        if (failure_stage) *failure_stage = "alpha mask arguments";
        return E_INVALIDARG;
    }
    return copy_impl(context, target, indices, palette, mask, target_width, target_height,
        source_rect, destination_rect, clip, std::nullopt, Sampling::SglFixedPoint,
        background, modulation_color, BlendMode::AlphaMask, failure_stage, nullptr);
}

HRESULT Compositor::copy_impl(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
    ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* palette,
    ID3D11ShaderResourceView* mask,
    UINT target_width, UINT target_height, Rect source_rect, Rect destination_rect, RECT clip,
    std::optional<std::uint32_t> color_key, Sampling sampling, ID3D11ShaderResourceView* background,
    std::uint32_t modulation_color, BlendMode blend, const char** failure_stage,
    const RendererSource* renderer_source) noexcept {
    if (!renderer_source) invalidate_renderer_bindings();
    PreparedDraw prepared;
    const HRESULT result = prepare(context, target, source, palette, mask, target_width, target_height, source_rect,
        destination_rect, clip, color_key, sampling, background, modulation_color, blend, failure_stage,
        renderer_source, prepared);
    if (FAILED(result) || !prepared.visible) return result;
    return record(context, &prepared, 1, renderer_source != nullptr);
}

HRESULT Compositor::prepare(ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
    ID3D11ShaderResourceView* source, ID3D11ShaderResourceView* palette,
    ID3D11ShaderResourceView* mask,
    UINT target_width, UINT target_height, Rect source_rect, Rect destination_rect, RECT clip,
    std::optional<std::uint32_t> color_key, Sampling sampling, ID3D11ShaderResourceView* background,
    std::uint32_t modulation_color, BlendMode blend, const char** failure_stage,
    const RendererSource* renderer_source, PreparedDraw& prepared) noexcept {
    prepared.visible = false;
    const auto fail = [failure_stage](const char* stage, HRESULT result) {
        if (failure_stage) *failure_stage = stage;
        return result;
    };
    if (failure_stage) *failure_stage = "compositor parameter validation";
    if (!context || !target || !source || !device_ || !vertex_shader_ || !pixel_shader_ || !keyed_pixel_shader_ ||
        !alpha_pixel_shader_ || !multiply_pixel_shader_ || !multiply_modulated_pixel_shader_ || !multiply2_pixel_shader_ || !fill_alpha_pixel_shader_ ||
        !additive_pixel_shader_ || !additive_modulated_pixel_shader_ || !instances_ || !input_layout_ || !indexed_shader_ || !keyed_indexed_shader_ ||
        !alpha_indexed_shader_ || !multiply_indexed_shader_ || !multiply_modulated_indexed_shader_ || !additive_indexed_shader_ ||
        !additive_modulated_indexed_shader_ || !alpha_masked_indexed_shader_ || !sampler_ || !present_sampler_ || !rasterizer_ ||
        !blend_source_shaders_[masked_source] || !blend_states_[alpha_mask_blend] || !target_width || !target_height ||
        !valid_rect(source_rect) || !valid_rect(destination_rect)) return fail("compositor invalid/null inputs or rectangles", E_INVALIDARG);
    if (blend != BlendMode::Opaque && blend != BlendMode::Alpha && blend != BlendMode::Multiply &&
        blend != BlendMode::MultiplyModulated && blend != BlendMode::Multiply2 &&
        blend != BlendMode::Additive && blend != BlendMode::AdditiveModulated && blend != BlendMode::AlphaMask &&
        blend != BlendMode::FillAlpha)
        return fail("unsupported compositor blend mode", E_INVALIDARG);
    if (blend == BlendMode::FillAlpha && palette) return fail("fill blend requires a BGRA source", E_INVALIDARG);
    // A blended draw with a background view reads the target's earlier pixels from it and reproduces the original
    // integer formulas; without one, a hardware blend state combines the draw with the target.
    if ((blend == BlendMode::Opaque && background) ||
        (blend != BlendMode::Opaque && color_key && !(palette && blend == BlendMode::Alpha)) ||
        (blend != BlendMode::Opaque && palette && blend != BlendMode::Alpha && blend != BlendMode::Multiply &&
            blend != BlendMode::MultiplyModulated && blend != BlendMode::Additive && blend != BlendMode::AdditiveModulated &&
            blend != BlendMode::AlphaMask) ||
        ((blend == BlendMode::AlphaMask) != (mask != nullptr)) ||
        (mask && !palette))
        return fail("incompatible blend, background, palette or color key", E_INVALIDARG);
    HRESULT result = S_OK;
    D3D11_TEXTURE2D_DESC texture_desc = {};
    D3D11_TEXTURE2D_DESC target_desc = {};
    ID3D11Resource* target_resource_identity = nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC source_view = {};
    D3D11_RENDER_TARGET_VIEW_DESC target_view = {};
    if (renderer_source) {
        // SurfaceRenderer owns these views and verifies their dimensions when each surface is reused.
        if (!renderer_source->width || !renderer_source->height ||
            (renderer_source->format != DXGI_FORMAT_R8_UINT &&
                renderer_source->format != DXGI_FORMAT_B8G8R8A8_UNORM))
            return fail("invalid renderer-owned source metadata", E_INVALIDARG);
        texture_desc.Width = renderer_source->width;
        texture_desc.Height = renderer_source->height;
        texture_desc.Format = renderer_source->format;
        target_desc.Width = target_width;
        target_desc.Height = target_height;
        target_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        source_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        source_view.Texture2D.MostDetailedMip = 0;
        source_view.Format = renderer_source->format;
        target_view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        target_view.Texture2D.MipSlice = 0;
        target_view.Format = target_desc.Format;
    } else {
        ComPtr<ID3D11Device> context_device;
        ComPtr<ID3D11Device> target_device;
        ComPtr<ID3D11Device> source_device;
        context->GetDevice(&context_device);
        target->GetDevice(&target_device);
        source->GetDevice(&source_device);
        if (context_device.Get() != device_.Get() || target_device.Get() != device_.Get() ||
            source_device.Get() != device_.Get()) return fail("compositor device mismatch", E_INVALIDARG);
        ComPtr<ID3D11Resource> source_resource;
        ComPtr<ID3D11Resource> target_resource;
        source->GetResource(&source_resource);
        target->GetResource(&target_resource);
        if (!source_resource || !target_resource || source_resource.Get() == target_resource.Get())
            return fail("compositor source/target resource identity", E_INVALIDARG);
        target_resource_identity = target_resource.Get();
        ComPtr<ID3D11Texture2D> texture;
        if (failure_stage) *failure_stage = "source texture query";
        result = source_resource.As(&texture);
        if (FAILED(result)) return fail("source texture query", result);
        ComPtr<ID3D11Texture2D> target_texture;
        if (failure_stage) *failure_stage = "target texture query";
        result = target_resource.As(&target_texture);
        if (FAILED(result)) return fail("target texture query", result);
        source->GetDesc(&source_view);
        target->GetDesc(&target_view);
        target_texture->GetDesc(&target_desc);
        texture->GetDesc(&texture_desc);
    }
    if (source_view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
        source_view.Texture2D.MostDetailedMip != 0 ||
        target_view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
        target_view.Texture2D.MipSlice != 0) return fail("unsupported SRV/RTV view", E_NOTIMPL);
    if (palette) {
        if (source_view.Format != DXGI_FORMAT_R8_UINT) return fail("unsupported indexed source format", E_NOTIMPL);
        if (!renderer_source) {
            ComPtr<ID3D11Device> palette_device;
            palette->GetDevice(&palette_device);
            if (palette_device.Get() != device_.Get()) return fail("palette device mismatch", E_INVALIDARG);
            D3D11_SHADER_RESOURCE_VIEW_DESC palette_view = {};
            palette->GetDesc(&palette_view);
            if (palette_view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
                palette_view.Texture2D.MostDetailedMip != 0 ||
                (palette_view.Format != DXGI_FORMAT_R8G8B8A8_UNORM && palette_view.Format != DXGI_FORMAT_B8G8R8A8_UNORM))
                return fail("unsupported palette view", E_NOTIMPL);
            ComPtr<ID3D11Resource> palette_resource;
            palette->GetResource(&palette_resource);
            if (!palette_resource || palette_resource.Get() == target_resource_identity)
                return fail("palette/target resource identity", E_INVALIDARG);
            ComPtr<ID3D11Texture2D> palette_texture;
            result = palette_resource.As(&palette_texture);
            if (FAILED(result)) return fail("palette texture query", result);
            D3D11_TEXTURE2D_DESC palette_desc = {};
            palette_texture->GetDesc(&palette_desc);
            if (palette_desc.Width != 256 || palette_desc.Height != 1) return fail("palette texture dimensions", E_INVALIDARG);
        }
    } else if (source_view.Format != DXGI_FORMAT_R8G8B8A8_UNORM && source_view.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        return fail("unsupported BGRA source format", E_NOTIMPL);
    if (target_view.Format != DXGI_FORMAT_R8G8B8A8_UNORM && target_view.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        return fail("unsupported target format", E_NOTIMPL);
    if (target_width != target_desc.Width || target_height != target_desc.Height)
        return fail("target dimensions do not match texture", E_INVALIDARG);
    if (background && !renderer_source) {
        ComPtr<ID3D11Device> background_device;
        background->GetDevice(&background_device);
        ComPtr<ID3D11Resource> resource;
        background->GetResource(&resource);
        ComPtr<ID3D11Texture2D> image;
        if (background_device.Get() != device_.Get() || !resource || resource.Get() == target_resource_identity ||
            FAILED(resource.As(&image))) return fail("background resource validation", E_INVALIDARG);
        D3D11_TEXTURE2D_DESC desc = {};
        image->GetDesc(&desc);
        if (desc.Width != target_width || desc.Height != target_height || desc.Format != target_desc.Format)
            return fail("background texture dimensions/format", E_INVALIDARG);
    }
    if (mask && !renderer_source) {
        D3D11_SHADER_RESOURCE_VIEW_DESC mask_view = {};
        mask->GetDesc(&mask_view);
        ComPtr<ID3D11Device> mask_device;
        ComPtr<ID3D11Resource> mask_resource;
        mask->GetDevice(&mask_device);
        mask->GetResource(&mask_resource);
        ComPtr<ID3D11Texture2D> mask_texture;
        if (mask_device.Get() != device_.Get() || !mask_resource || mask_resource.Get() == target_resource_identity ||
            mask_view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            mask_view.Texture2D.MostDetailedMip != 0 || mask_view.Format != DXGI_FORMAT_R8_UINT ||
            FAILED(mask_resource.As(&mask_texture)))
            return fail("unsupported alpha mask view", E_INVALIDARG);
        D3D11_TEXTURE2D_DESC mask_desc = {};
        mask_texture->GetDesc(&mask_desc);
        if (mask_desc.Width != texture_desc.Width || mask_desc.Height != texture_desc.Height)
            return fail("alpha mask dimensions do not match source", E_INVALIDARG);
    }
    if (source_rect.left < 0 || source_rect.top < 0 ||
        source_rect.right > static_cast<float>(texture_desc.Width) ||
        source_rect.bottom > static_cast<float>(texture_desc.Height)) return fail("source rectangle outside texture", E_INVALIDARG);
    UINT fixed_origin[2] = {};
    UINT fixed_step[2] = {};
    float fractional_step[2] = {};
    std::uint32_t fixed_sampling = 0;
    float sample_origin[2] = {source_rect.left, source_rect.top};
    if (sampling == Sampling::PresentSharp) {
        if (palette || mask) return fail("present sampling requires a BGRA source", E_NOTIMPL);
        fixed_sampling = 4;
    } else if (sampling == Sampling::SglFixedPoint || sampling == Sampling::SglBilinear) {
        const float source_values[] = {source_rect.left, source_rect.top, source_rect.right, source_rect.bottom};
        for (float value : source_values) {
            if (value < 0 || value > 65535)
                return fail("out-of-range SGL source rectangle", E_INVALIDARG);
        }
        const float destination_values[] = {destination_rect.left, destination_rect.top,
            destination_rect.right, destination_rect.bottom};
        bool fractional_destination = false;
        for (float value : destination_values) {
            if (value < -65535 || value > 65535)
                return fail("out-of-range SGL destination rectangle", E_INVALIDARG);
            fractional_destination |= std::floor(value) != value;
        }
        const float source_width = source_rect.right - source_rect.left;
        const float source_height = source_rect.bottom - source_rect.top;
        const float destination_width = destination_rect.right - destination_rect.left;
        const float destination_height = destination_rect.bottom - destination_rect.top;
        if (source_width > 65535 || source_height > 65535 || destination_width > 65535 || destination_height > 65535)
            return fail("SGL fixed rectangle extent out of range", E_INVALIDARG);
        fixed_origin[0] = static_cast<UINT>(std::floor(source_rect.left));
        fixed_origin[1] = static_cast<UINT>(std::floor(source_rect.top));
        const bool fractional_source = std::floor(source_rect.left) != source_rect.left ||
            std::floor(source_rect.top) != source_rect.top || std::floor(source_rect.right) != source_rect.right ||
            std::floor(source_rect.bottom) != source_rect.bottom;
        if (sampling == Sampling::SglBilinear && (palette || fractional_source || fractional_destination))
            return fail("SGL bilinear sampling requires integral BGRA rectangles", E_NOTIMPL);
        if (fractional_source || fractional_destination) {
            fixed_sampling = 2;
            fractional_step[0] = source_width / destination_width;
            fractional_step[1] = source_height / destination_height;
        } else {
            fixed_sampling = 1;
            fixed_step[0] = (static_cast<UINT>(source_width) << 16) / static_cast<UINT>(destination_width);
            fixed_step[1] = (static_cast<UINT>(source_height) << 16) / static_cast<UINT>(destination_height);
            if (sampling == Sampling::SglBilinear) {
                fixed_sampling = 3;
                sample_origin[0] = std::max({destination_rect.left, static_cast<float>(clip.left), 0.0f});
                sample_origin[1] = std::max({destination_rect.top, static_cast<float>(clip.top), 0.0f});
                const float destination_origin[] = {destination_rect.left, destination_rect.top};
                for (unsigned axis = 0; axis < 2; ++axis) {
                    const auto skipped = static_cast<UINT>(sample_origin[axis] - destination_origin[axis]);
                    fixed_origin[axis] = ((fixed_origin[axis] << 16) + skipped * fixed_step[axis] +
                        (fixed_step[axis] >> 1)) >> 1;
                    fixed_step[axis] >>= 1;
                }
            }
        }
    }
    clip.left = std::max<LONG>(clip.left, 0);
    clip.top = std::max<LONG>(clip.top, 0);
    clip.right = std::min<LONG>(clip.right, static_cast<LONG>(target_width));
    clip.bottom = std::min<LONG>(clip.bottom, static_cast<LONG>(target_height));
    if (clip.left >= clip.right || clip.top >= clip.bottom) return S_OK;
    const float width = static_cast<float>(texture_desc.Width);
    const float height = static_cast<float>(texture_desc.Height);
    const float modulation[4] = {
        static_cast<float>((modulation_color >> 16) & 0xff) / 255.0f,
        static_cast<float>((modulation_color >> 8) & 0xff) / 255.0f,
        static_cast<float>(modulation_color & 0xff) / 255.0f,
        static_cast<float>((modulation_color >> 24) & 0xff) / 255.0f,
    };
    const Geometry geometry = {
        {destination_rect.left, destination_rect.top, destination_rect.right, destination_rect.bottom},
        {source_rect.left / width, source_rect.top / height, source_rect.right / width, source_rect.bottom / height},
        {static_cast<float>(target_width), static_cast<float>(target_height)}, {sample_origin[0], sample_origin[1]},
        color_key.value_or(no_color_key), fixed_sampling,
        {fixed_origin[0], fixed_origin[1]}, {fixed_step[0], fixed_step[1]},
        {fractional_step[0], fractional_step[1]},
        {modulation[0], modulation[1], modulation[2], modulation[3]},
    };
    const bool hardware = blend != BlendMode::Opaque && !background;
    ID3D11PixelShader* pixel_shader = mask ? alpha_masked_indexed_shader_.Get() : palette ?
        (blend == BlendMode::MultiplyModulated ? multiply_modulated_indexed_shader_.Get() :
            blend == BlendMode::Multiply ? multiply_indexed_shader_.Get() :
            blend == BlendMode::AdditiveModulated ? additive_modulated_indexed_shader_.Get() :
            blend == BlendMode::Additive ? additive_indexed_shader_.Get() :
            blend == BlendMode::Alpha ? alpha_indexed_shader_.Get() :
            color_key ? keyed_indexed_shader_.Get() : indexed_shader_.Get()) :
        blend == BlendMode::Alpha ? alpha_pixel_shader_.Get() :
        blend == BlendMode::MultiplyModulated ? multiply_modulated_pixel_shader_.Get() :
        blend == BlendMode::Multiply ? multiply_pixel_shader_.Get() :
        blend == BlendMode::Multiply2 ? multiply2_pixel_shader_.Get() :
        blend == BlendMode::FillAlpha ? fill_alpha_pixel_shader_.Get() :
        blend == BlendMode::AdditiveModulated ? additive_modulated_pixel_shader_.Get() :
        blend == BlendMode::Additive ? additive_pixel_shader_.Get() :
        color_key ? keyed_pixel_shader_.Get() : pixel_shader_.Get();
    ID3D11BlendState* blend_state = nullptr;
    std::uint8_t blend_factor = 0xff;
    if (hardware) {
        BlendSource source_kind = plain_source;
        HardwareBlend state = alpha_blend;
        switch (blend) {
        case BlendMode::Alpha:
            source_kind = palette ? keyed_tinted_indexed_source : tinted_source;
            break;
        case BlendMode::Multiply:
            source_kind = palette ? plain_indexed_source : plain_source;
            state = multiply_blend;
            break;
        case BlendMode::MultiplyModulated:
            source_kind = palette ? tinted_indexed_source : tinted_source;
            state = multiply_blend;
            break;
        case BlendMode::Multiply2:
            state = multiply2_blend;
            break;
        case BlendMode::Additive:
            source_kind = palette ? rgb_indexed_source : rgb_source;
            state = additive_blend;
            break;
        case BlendMode::AdditiveModulated:
            source_kind = palette ? tinted_rgb_indexed_source : tinted_rgb_source;
            state = additive_blend;
            break;
        case BlendMode::FillAlpha:
            source_kind = rgb_source;
            state = fill_alpha_blend;
            blend_factor = static_cast<std::uint8_t>(modulation_color >> 24);
            break;
        case BlendMode::AlphaMask:
        default:
            source_kind = masked_source;
            state = alpha_mask_blend;
            break;
        }
        pixel_shader = blend_source_shaders_[source_kind].Get();
        blend_state = blend_states_[state].Get();
    }
    static_assert(sizeof(Geometry) == sizeof(prepared.constants));
    std::memcpy(prepared.constants, &geometry, sizeof(geometry));
    prepared.target = target;
    prepared.target_width = target_width;
    prepared.target_height = target_height;
    prepared.clip = clip;
    prepared.pixel_shader = pixel_shader;
    prepared.blend_state = blend_state;
    prepared.blend_factor = blend_factor;
    ID3D11ShaderResourceView* const resources[] = {palette ? nullptr : source, palette ? source : nullptr, palette, background, mask};
    std::memcpy(prepared.resources, resources, sizeof(resources));
    prepared.visible = true;
    return S_OK;
}

HRESULT Compositor::record(ID3D11DeviceContext* context, const PreparedDraw* draws, std::size_t count,
    bool renderer_owned, unsigned* draw_calls) noexcept {
    HRESULT result = S_OK;
    while (count && SUCCEEDED(result)) {
        const UINT batch = static_cast<UINT>(std::min<std::size_t>(count, instance_capacity));
        // Appending never touches instances that recorded draws still read; only a full ring is discarded.
        const bool restart = instance_capacity - instance_position_ < batch;
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        result = context->Map(instances_.Get(), 0, restart ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE,
            0, &mapped);
        if (FAILED(result)) break;
        if (restart) instance_position_ = 0;
        auto* const output = static_cast<std::uint8_t*>(mapped.pData) + std::size_t{instance_position_} * sizeof(Instance);
        for (UINT index = 0; index < batch; ++index) {
            Instance instance;
            static_assert(sizeof(instance.geometry) == sizeof(draws[index].constants));
            std::memcpy(&instance.geometry, draws[index].constants, sizeof(instance.geometry));
            const RECT& clip = draws[index].clip;
            instance.clip[0] = clip.left;
            instance.clip[1] = clip.top;
            instance.clip[2] = clip.right;
            instance.clip[3] = clip.bottom;
            std::memcpy(output + std::size_t{index} * sizeof(Instance), &instance, sizeof(instance));
        }
        context->Unmap(instances_.Get(), 0);
        // Renderer-owned draws keep these bindings; public compositor calls invalidate the cache.
        const bool bind_renderer_state = !renderer_owned || renderer_state_context_ != context;
        if (bind_renderer_state) {
            context->RSSetState(rasterizer_.Get());
            context->IASetInputLayout(input_layout_.Get());
            ID3D11Buffer* buffer = instances_.Get();
            const UINT stride = sizeof(Instance);
            const UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);
            ID3D11SamplerState* samplers[] = {sampler_.Get(), present_sampler_.Get()};
            context->PSSetSamplers(0, 2, samplers);
            renderer_state_context_ = renderer_owned ? context : nullptr;
            renderer_pixel_shader_ = nullptr;
            renderer_target_ = nullptr;
            renderer_viewport_width_ = renderer_viewport_height_ = 0;
            renderer_blend_bound_ = false;
            for (auto& resource : renderer_resources_) resource = reinterpret_cast<ID3D11ShaderResourceView*>(~std::uintptr_t{0});
        }
        for (UINT first = 0; first < batch;) {
            const PreparedDraw& draw = draws[first];
            UINT run = 1;
            while (first + run < batch && same_state(draws[first + run], draw)) ++run;
            ID3D11RenderTargetView* const target = draw.target;
            if (renderer_target_ != target) {
                context->OMSetRenderTargets(1, &target, nullptr);
                renderer_target_ = target;
                // Binding a target unbinds any shader resource view of the same texture.
                for (auto& resource : renderer_resources_) resource = reinterpret_cast<ID3D11ShaderResourceView*>(~std::uintptr_t{0});
            }
            if (renderer_viewport_width_ != draw.target_width || renderer_viewport_height_ != draw.target_height) {
                const D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(draw.target_width),
                    static_cast<float>(draw.target_height), 0, 1};
                context->RSSetViewports(1, &viewport);
                renderer_viewport_width_ = draw.target_width;
                renderer_viewport_height_ = draw.target_height;
            }
            if (!renderer_blend_bound_ || renderer_blend_state_ != draw.blend_state ||
                (draw.blend_state && renderer_blend_factor_ != draw.blend_factor)) {
                const float factor = static_cast<float>(draw.blend_factor) / 255.0f;
                const float factors[] = {factor, factor, factor, factor};
                context->OMSetBlendState(draw.blend_state, factors, 0xffffffff);
                renderer_blend_state_ = draw.blend_state;
                renderer_blend_factor_ = draw.blend_factor;
                renderer_blend_bound_ = true;
            }
            if (renderer_pixel_shader_ != draw.pixel_shader) {
                context->PSSetShader(draw.pixel_shader, nullptr, 0);
                renderer_pixel_shader_ = draw.pixel_shader;
            }
            if (std::memcmp(draw.resources, renderer_resources_, sizeof(draw.resources)) != 0) {
                context->PSSetShaderResources(0, 5, draw.resources);
                std::memcpy(renderer_resources_, draw.resources, sizeof(draw.resources));
            }
            context->DrawInstanced(6, run, 0, instance_position_ + first);
            if (draw_calls) ++*draw_calls;
            first += run;
        }
        instance_position_ += batch;
        draws += batch;
        count -= batch;
    }
    if (!renderer_owned) {
        ID3D11ShaderResourceView* empty[] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        context->PSSetShaderResources(0, 5, empty);
        invalidate_renderer_bindings();
    }
    return result;
}

void Compositor::invalidate_renderer_bindings() noexcept {
    renderer_state_context_ = nullptr;
    renderer_pixel_shader_ = nullptr;
    renderer_target_ = nullptr;
    renderer_viewport_width_ = renderer_viewport_height_ = 0;
    renderer_blend_bound_ = false;
    for (auto& resource : renderer_resources_) resource = nullptr;
}

}
