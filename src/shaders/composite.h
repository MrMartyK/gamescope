#include "colorimetry.h"

#include "shaderfilter.h"
#include "alphamode.h"

vec4 sampleRegular(sampler2D tex, vec2 coord, uint colorspace) {
    vec4 color = textureLod(tex, coord, 0);
    color.rgb = colorspace_plane_degamma_tf(color.rgb, colorspace);
    return color;
}

// To be considered pseudo-bandlimited, upscaling factor must be at least 2x.

const float bandlimited_PI = 3.14159265359;
const float bandlimited_PI_half = 0.5 * bandlimited_PI;
// size: resolution of sampled texture
// inv_size: inverse resolution of sampled texture
// extent: Screen-space gradient of UV in texels. Typically computed as (texture resolution) / (viewport resolution).
//   If screen is rotated by 90 or 270 degrees, the derivatives need to be computed appropriately.
//   For uniform scaling, none of this matters.
//   extent can be multiplied to achieve LOD bias.
//   extent must be at least 1.0 / 256.0.
vec4 sampleBandLimited(sampler2D samp, vec2 uv, vec2 size, vec2 inv_size, vec2 extent, uint colorspace, bool unnormalized)
{
    // Josh:
    // Clamp to behaviour like 4x scale (0.25).
    //
    // Was defaulted to 2x before (0.5), which is 1px, but gives blurry result
    // on Cave Story (480p) -> 800p on Deck.
    // TODO: Maybe make this configurable?
    const float max_extent = 0.25f;

	// Get base pixel and phase, range [0, 1).
	vec2 pixel = uv * (unnormalized ? vec2(1.0f) : size) - 0.5;
	vec2 base_pixel = floor(pixel);
	vec2 phase = pixel - base_pixel;

	// We can resolve the filter by just sampling a single 2x2 block.
	// Lerp between normal sampling at LOD 0, and bandlimited pixel filter at LOD -1.
	vec2 shift = 0.5 + 0.5 * sin(bandlimited_PI_half * clamp((phase - 0.5) / min(extent, vec2(max_extent)), -1.0, 1.0));
	uv = (base_pixel + 0.5 + shift) * (unnormalized ? vec2(1.0f) : inv_size);

	return sampleRegular(samp, uv, colorspace);
}

uint pseudo_random(uint seed) {
    seed ^= (seed << 13);
    seed ^= (seed >> 17);
    seed ^= (seed << 5);
    return seed * 1664525u + 1013904223u;
}

void compositing_debug(uvec2 coord) {
    uvec2 pos = coord;
    pos.x -= (u_frameId & 2) != 0 ?  128 : 0;
    pos.y -= (u_frameId & 1) != 0 ?  128 : 0;

    if (pos.x >= 40 && pos.x < 120 && pos.y >= 40 && pos.y < 120) {
        vec4 value = vec4(1.0f, 1.0f, 1.0f, 1.0f);
        if (checkDebugFlag(compositedebug_Markers_Partial)) {
            value = vec4(0.0f, 1.0f, 1.0f, 1.0f);
        }
        if (pos.x >= 48 && pos.x < 112 && pos.y >= 48 && pos.y < 112) {
            uint random = pseudo_random(u_frameId.x + (pos.x & ~0x7) + (pos.y & ~0x7) * 50);
            vec4 time = round(unpackUnorm4x8(random)).xyzw;
            if (time.x + time.y + time.z + time.w < 2.0f)
                value = vec4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        imageStore(dst, ivec2(coord), value);
    }
}

// ---- ITM Debug Overlay ----
// Persistent on-screen panel showing live ITM status and stats.
// Renders a 3x5 bitmap font at 3x scale for readability.
//
// Each glyph is 3 columns x 5 rows = 15 bits.
// Packed as: row0(bits 14..12) row1(11..9) row2(8..6) row3(5..3) row4(2..0)
// MSB of each 3-bit group = leftmost column.
//
//  Glyph grid example for '0':
//   ###    row0 = 111 = 7
//   # #    row1 = 101 = 5
//   # #    row2 = 101 = 5
//   # #    row3 = 101 = 5
//   ###    row4 = 111 = 7
// Helper macro: G(r0,r1,r2,r3,r4) = (r0<<12)|(r1<<9)|(r2<<6)|(r3<<3)|r4

#define GLYPH(r0,r1,r2,r3,r4) ((uint(r0)<<12)|(uint(r1)<<9)|(uint(r2)<<6)|(uint(r3)<<3)|uint(r4))

uint font_glyph(uint ch) {
    //          row0 row1 row2 row3 row4
    if (ch == 0u)  return GLYPH(7, 5, 5, 5, 7); // 0:  ### / # # / # # / # # / ###
    if (ch == 1u)  return GLYPH(2, 6, 2, 2, 7); // 1:   #  / ##  /  #  /  #  / ###
    if (ch == 2u)  return GLYPH(7, 1, 7, 4, 7); // 2:  ### /   # / ### / #   / ###
    if (ch == 3u)  return GLYPH(7, 1, 7, 1, 7); // 3:  ### /   # / ### /   # / ###
    if (ch == 4u)  return GLYPH(5, 5, 7, 1, 1); // 4:  # # / # # / ### /   # /   #
    if (ch == 5u)  return GLYPH(7, 4, 7, 1, 7); // 5:  ### / #   / ### /   # / ###
    if (ch == 6u)  return GLYPH(7, 4, 7, 5, 7); // 6:  ### / #   / ### / # # / ###
    if (ch == 7u)  return GLYPH(7, 1, 1, 1, 1); // 7:  ### /   # /   # /   # /   #
    if (ch == 8u)  return GLYPH(7, 5, 7, 5, 7); // 8:  ### / # # / ### / # # / ###
    if (ch == 9u)  return GLYPH(7, 5, 7, 1, 7); // 9:  ### / # # / ### /   # / ###

    // Letters (10+)
    if (ch == 10u) return GLYPH(2, 5, 7, 5, 5); // A:   #  / # # / ### / # # / # #
    if (ch == 11u) return GLYPH(7, 5, 7, 4, 7); // D:  ### / # # / ### / #   / ### -- crude D
    if (ch == 12u) return GLYPH(7, 4, 7, 4, 7); // E:  ### / #   / ### / #   / ###
    if (ch == 13u) return GLYPH(7, 4, 7, 4, 4); // F:  ### / #   / ### / #   / #
    if (ch == 14u) return GLYPH(7, 4, 5, 5, 7); // G:  ### / #   / # # / # # / ###
    if (ch == 15u) return GLYPH(5, 5, 7, 5, 5); // H:  # # / # # / ### / # # / # #
    if (ch == 16u) return GLYPH(7, 2, 2, 2, 7); // I:  ### /  #  /  #  /  #  / ###
    if (ch == 17u) return GLYPH(4, 4, 4, 4, 7); // L:  #   / #   / #   / #   / ###
    if (ch == 18u) return GLYPH(5, 7, 5, 5, 5); // M:  # # / ### / # # / # # / # #
    if (ch == 19u) return GLYPH(6, 5, 5, 5, 5); // N:  ##  / # # / # # / # # / # #
    if (ch == 20u) return GLYPH(7, 5, 5, 5, 7); // O:  ### / # # / # # / # # / ###
    if (ch == 21u) return GLYPH(7, 5, 7, 4, 4); // P:  ### / # # / ### / #   / #
    if (ch == 22u) return GLYPH(7, 5, 7, 1, 1); // Q:  ### / # # / ### /   # /   # -- crude
    if (ch == 23u) return GLYPH(7, 5, 7, 6, 5); // R:  ### / # # / ### / ##  / # #
    if (ch == 24u) return GLYPH(7, 4, 7, 1, 7); // S:  ### / #   / ### /   # / ###  (= 5)
    if (ch == 25u) return GLYPH(7, 2, 2, 2, 2); // T:  ### /  #  /  #  /  #  /  #
    if (ch == 26u) return GLYPH(5, 5, 5, 5, 7); // U:  # # / # # / # # / # # / ###
    if (ch == 27u) return GLYPH(0, 0, 7, 0, 0); // -:  (dash, middle row)
    if (ch == 28u) return 0u;                     // (space)
    return 0u;
}

bool font_pixel(uint ch, uint lx, uint ly) {
    uint glyph = font_glyph(ch);
    uint row = ly / 3u;  // 0..4  (3x scale vertically)
    uint col = lx / 3u;  // 0..2  (3x scale horizontally)
    if (row > 4u || col > 2u) return false;
    uint bit = (4u - row) * 3u + (2u - col);
    return (glyph & (1u << bit)) != 0u;
}

// Character constants
const uint CH_0 = 0u;  const uint CH_1 = 1u;  const uint CH_2 = 2u;
const uint CH_3 = 3u;  const uint CH_4 = 4u;  const uint CH_5 = 5u;
const uint CH_6 = 6u;  const uint CH_7 = 7u;  const uint CH_8 = 8u;
const uint CH_9 = 9u;
const uint CH_A = 10u; const uint CH_D = 11u; const uint CH_E = 12u;
const uint CH_F = 13u; const uint CH_G = 14u; const uint CH_H = 15u;
const uint CH_I = 16u; const uint CH_L = 17u; const uint CH_M = 18u;
const uint CH_N = 19u; const uint CH_O = 20u; const uint CH_P = 21u;
const uint CH_Q = 22u; const uint CH_R = 23u; const uint CH_S = 24u;
const uint CH_T = 25u; const uint CH_U = 26u;
const uint CH_DASH = 27u; const uint CH_SPC = 28u;

uint digit_char(uint value, uint pos, uint total_digits) {
    uint divisor = 1u;
    for (uint i = 0u; i < (total_digits - 1u - pos); i++)
        divisor *= 10u;
    return (value / divisor) % 10u;
}

void itm_debug_indicator(uvec2 coord) {
    uvec2 outSize = imageSize(dst);

    // Each glyph: 3px * 3x scale = 9px wide, 5px * 3x scale = 15px tall
    const uint GLYPH_W  = 9u;   // 3 cols * 3x
    const uint GLYPH_H  = 15u;  // 5 rows * 3x
    const uint GLYPH_GAP = 3u;  // 3px gap between glyphs
    const uint CELL_W   = GLYPH_W + GLYPH_GAP; // 12px per character cell
    const uint LINE_H   = GLYPH_H + 4u;        // 19px per line (15 + 4 spacing)
    const uint MAX_COLS  = 10u;
    const uint NUM_LINES = 5u;
    const uint PAD       = 8u;

    uint panelW = MAX_COLS * CELL_W + PAD * 2u;
    uint panelH = NUM_LINES * LINE_H + PAD * 2u;
    uint margin = 12u;

    uint px = outSize.x - margin - panelW;
    uint py = margin;

    if (coord.x < px || coord.x >= px + panelW ||
        coord.y < py || coord.y >= py + panelH)
        return;

    uint lx = coord.x - px;
    uint ly = coord.y - py;

    vec4 bgColor = vec4(0.05f, 0.05f, 0.05f, 1.0f);

    // Border (2px)
    if (lx < 2u || lx >= panelW - 2u || ly < 2u || ly >= panelH - 2u) {
        vec4 borderColor = c_itm_enable
            ? vec4(0.1f, 0.7f, 0.1f, 1.0f)
            : vec4(0.5f, 0.15f, 0.15f, 1.0f);
        imageStore(dst, ivec2(coord), borderColor);
        return;
    }

    // Content area
    uint cx = lx - PAD;
    uint cy = ly - PAD;
    if (lx < PAD || ly < PAD) {
        imageStore(dst, ivec2(coord), bgColor);
        return;
    }

    uint row = cy / LINE_H;
    uint rowY = cy % LINE_H;
    uint col = cx / CELL_W;
    uint colX = cx % CELL_W;

    if (rowY >= GLYPH_H || row >= NUM_LINES || col >= MAX_COLS || cx >= MAX_COLS * CELL_W) {
        imageStore(dst, ivec2(coord), bgColor);
        return;
    }

    // In the gap between glyphs
    if (colX >= GLYPH_W) {
        imageStore(dst, ivec2(coord), bgColor);
        return;
    }

    uint ch = CH_SPC;
    vec4 textColor = vec4(0.9f, 0.9f, 0.9f, 1.0f);

    // Line 0: "ITM ON" or "ITM OFF"
    if (row == 0u) {
        if (c_itm_enable) {
            // I T M   O N
            const uint line0[10] = uint[10](CH_I, CH_T, CH_M, CH_SPC, CH_O, CH_N, CH_SPC, CH_SPC, CH_SPC, CH_SPC);
            ch = line0[col];
            textColor = vec4(0.2f, 1.0f, 0.3f, 1.0f);
        } else {
            const uint line0[10] = uint[10](CH_I, CH_T, CH_M, CH_SPC, CH_O, CH_F, CH_F, CH_SPC, CH_SPC, CH_SPC);
            ch = line0[col];
            textColor = vec4(1.0f, 0.3f, 0.3f, 1.0f);
        }
    }
    // Line 1: "SDR <nnn>"  (u_itmSdrNits)
    else if (row == 1u) {
        textColor = vec4(0.6f, 0.85f, 1.0f, 1.0f);
        uint sdrNits = uint(clamp(u_itmSdrNits, 0.0f, 9999.0f));
        uint line1[10] = uint[10](CH_S, CH_D, CH_R, CH_SPC,
            digit_char(sdrNits, 0u, 4u), digit_char(sdrNits, 1u, 4u),
            digit_char(sdrNits, 2u, 4u), digit_char(sdrNits, 3u, 4u),
            CH_SPC, CH_SPC);
        ch = line1[col];
    }
    // Line 2: "TGT <nnnn>"  (u_itmTargetNits)
    else if (row == 2u) {
        textColor = vec4(1.0f, 0.9f, 0.4f, 1.0f);
        uint tgtNits = uint(clamp(u_itmTargetNits, 0.0f, 9999.0f));
        uint line2[10] = uint[10](CH_T, CH_G, CH_T, CH_SPC,
            digit_char(tgtNits, 0u, 4u), digit_char(tgtNits, 1u, 4u),
            digit_char(tgtNits, 2u, 4u), digit_char(tgtNits, 3u, 4u),
            CH_SPC, CH_SPC);
        ch = line2[col];
    }
    // Line 3: "EOTF PQ" or "EOTF G22"
    else if (row == 3u) {
        textColor = vec4(0.85f, 0.7f, 1.0f, 1.0f);
        if (c_output_eotf == uint(EOTF_PQ)) {
            const uint line3[10] = uint[10](CH_E, CH_O, CH_T, CH_F, CH_SPC, CH_P, CH_Q, CH_SPC, CH_SPC, CH_SPC);
            ch = line3[col];
        } else {
            uint line3[10] = uint[10](CH_E, CH_O, CH_T, CH_F, CH_SPC, CH_G, CH_2, CH_2, CH_SPC, CH_SPC);
            ch = line3[col];
        }
    }
    // Line 4: "N <count>"  (layer count)
    else if (row == 4u) {
        textColor = vec4(0.7f, 0.7f, 0.7f, 1.0f);
        uint layers = uint(c_layerCount);
        uint line4[10] = uint[10](CH_L, CH_DASH, digit_char(layers, 0u, 1u), CH_SPC, CH_SPC, CH_SPC, CH_SPC, CH_SPC, CH_SPC, CH_SPC);
        ch = line4[col];
    }

    if (font_pixel(ch, colX, rowY)) {
        imageStore(dst, ivec2(coord), textColor);
    } else {
        imageStore(dst, ivec2(coord), bgColor);
    }
}

// Takes in a scRGB/Linear encoded value and applies color management
// based on the input colorspace.
//
// ie. call colorspace_plane_degamma_tf(color.rgb, colorspace) before
// input to this function.
vec3 apply_layer_color_mgmt(vec3 color, uint layer, uint colorspace) {
    if (colorspace == colorspace_passthru)
        return color;

    if (c_itm_enable)
    {
        color = bt2446a_inverse_tonemapping(color, u_itmSdrNits, u_itmTargetNits);
        colorspace = colorspace_pq;
    }

    // Shaper + 3D LUT path to match DRM.
    uint plane_eotf = colorspace_to_eotf(colorspace);

    if (layer == 0 && checkDebugFlag(compositedebug_Heatmap))
    {
        // Debug HDR heatmap.
        color = hdr_heatmap(color, colorspace);
        plane_eotf = EOTF_Gamma22;
    }


    // The shaper TF is basically just a regamma to get into something the shaper LUT can handle.
    //
    // Despite naming, degamma + shaper TF are NOT necessarily the inverse of each other. ^^^
    // This gets the color ready to go into the shaper LUT.
    // ie. scRGB -> PQ
    //
    // We also need to do degamma here for non-linear views to blend in linear space.
    // ie. PQ -> PQ would need us to manually do bilinear here.
    bool lut3d_enabled = textureQueryLevels(s_shaperLut[plane_eotf]) != 0;
    if (lut3d_enabled)
    {
        color = colorspace_plane_shaper_tf(color, colorspace);
        color = perform_1dlut(color, s_shaperLut[plane_eotf]);
        color = perform_3dlut(color, s_lut3D[plane_eotf]);
        color = colorspace_blend_tf(color, c_output_eotf);
    }

    return color;
}

vec4 sampleBilinear(sampler2D tex, vec2 coord, uint colorspace, bool unnormalized) {
    vec2 scale = unnormalized ? vec2(1.0) : vec2(textureSize(tex, 0));

    vec2 pixCoord = coord * scale - 0.5f;
    vec2 originPixCoord = floor(pixCoord);

    vec2 gatherUV = (originPixCoord * scale + 1.0f) / scale;

    vec4 red   = textureGather(tex, gatherUV, 0);
    vec4 green = textureGather(tex, gatherUV, 1);
    vec4 blue  = textureGather(tex, gatherUV, 2);
    vec4 alpha = textureGather(tex, gatherUV, 3);

    vec4 c00 = vec4(red.w, green.w, blue.w, alpha.w);
    vec4 c01 = vec4(red.x, green.x, blue.x, alpha.x);
    vec4 c11 = vec4(red.y, green.y, blue.y, alpha.y);
    vec4 c10 = vec4(red.z, green.z, blue.z, alpha.z);

    c00.rgb = colorspace_plane_degamma_tf(c00.rgb, colorspace);
    c01.rgb = colorspace_plane_degamma_tf(c01.rgb, colorspace);
    c11.rgb = colorspace_plane_degamma_tf(c11.rgb, colorspace);
    c10.rgb = colorspace_plane_degamma_tf(c10.rgb, colorspace);

    vec2 filterWeight = pixCoord - originPixCoord;

    vec4 temp0 = mix(c01, c11, filterWeight.x);
    vec4 temp1 = mix(c00, c10, filterWeight.x);
    return mix(temp1, temp0, filterWeight.y);
}

vec4 sampleLayerEx(sampler2D layerSampler, uint offsetLayerIdx, uint colorspaceLayerIdx, vec2 uv, bool unnormalized) {
    vec2 coord = ((uv + u_offset[offsetLayerIdx]) * u_scale[offsetLayerIdx]);
    vec2 texSize = textureSize(layerSampler, 0);

    if (coord.x < 0.0f       || coord.y < 0.0f ||
        coord.x >= texSize.x || coord.y >= texSize.y) {
        float border = (u_borderMask & (1u << offsetLayerIdx)) != 0 ? 1.0f : 0.0f;

        if (checkDebugFlag(compositedebug_PlaneBorders))
            return vec4(vec3(1.0f, 0.0f, 1.0f) * border, border);

        return vec4(0.0f, 0.0f, 0.0f, border);
    }

    if (!unnormalized)
        coord /= texSize;

    uint colorspace = get_layer_colorspace(colorspaceLayerIdx);
    vec4 color;
    if (get_layer_shaderfilter(offsetLayerIdx) == filter_pixel) {
        vec2 output_res = texSize / u_scale[offsetLayerIdx];
        vec2 extent = max((texSize / output_res), vec2(1.0 / 256.0));
        color = sampleBandLimited(layerSampler, coord, unnormalized ? vec2(1.0f) : texSize, unnormalized ? vec2(1.0f) : vec2(1.0f) / texSize, extent, colorspace, unnormalized);
    }
    else if (get_layer_shaderfilter(offsetLayerIdx) == filter_linear_emulated) {
        color = sampleBilinear(layerSampler, coord, colorspace, unnormalized);
    }
    else {
        color = sampleRegular(layerSampler, coord, colorspace);
    }
    // JoshA: AMDGPU applies 3x4 CTM like this, where A is 1.0, but it only affects .rgb.
    color.rgb = vec4(color.rgb, 1.0f) * u_ctm[colorspaceLayerIdx];
    color.rgb = apply_layer_color_mgmt(color.rgb, offsetLayerIdx, colorspace);

    return color;
}

vec4 sampleLayer(sampler2D layerSampler, uint layerIdx, vec2 uv, bool unnormalized) {
    return sampleLayerEx(layerSampler, layerIdx, layerIdx, uv, unnormalized);
}

vec3 encodeOutputColor(vec3 value) {
    return colorspace_output_tf(value, c_output_eotf);
}
