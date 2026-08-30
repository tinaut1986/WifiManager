#pragma once
#include <citro2d.h>

// -----------------------------------------------------------------------------
//  Modern flat theme. One place to tweak the whole look of the app.
// -----------------------------------------------------------------------------

namespace theme
{
    // Surfaces
    constexpr u32 bg          = C2D_Color32(0x0F, 0x14, 0x1B, 0xFF); // near-black blue
    constexpr u32 surface     = C2D_Color32(0x1B, 0x24, 0x30, 0xFF);
    constexpr u32 surfaceAlt  = C2D_Color32(0x23, 0x30, 0x40, 0xFF);
    constexpr u32 overlay     = C2D_Color32(0x00, 0x00, 0x00, 0xB0);

    // Accents
    constexpr u32 accent      = C2D_Color32(0x3A, 0x9B, 0xFF, 0xFF); // blue
    constexpr u32 accentDim   = C2D_Color32(0x24, 0x5A, 0x8C, 0xFF);
    constexpr u32 good        = C2D_Color32(0x3B, 0xD1, 0x7E, 0xFF); // green
    constexpr u32 warn        = C2D_Color32(0xFF, 0xC1, 0x4E, 0xFF); // amber
    constexpr u32 danger      = C2D_Color32(0xFF, 0x5C, 0x5C, 0xFF); // red

    // Text
    constexpr u32 text        = C2D_Color32(0xF2, 0xF5, 0xF8, 0xFF);
    constexpr u32 textDim     = C2D_Color32(0x9A, 0xA7, 0xB4, 0xFF);
    constexpr u32 textOnAccent= C2D_Color32(0x0B, 0x12, 0x1A, 0xFF);

    // Metrics
    constexpr float pad       = 10.0f;
    constexpr float radius    = 8.0f;
    constexpr float headerH   = 28.0f;
    constexpr float hintH     = 20.0f;
}
