#pragma once
#include <citro2d.h>

// -----------------------------------------------------------------------------
//  Theme system: Dark, Light, and Midnight color palettes.
// -----------------------------------------------------------------------------

namespace theme
{
    enum class Type {
        Dark = 0,
        Light = 1,
        Midnight = 2,
    };

    // Dynamic surfaces
    extern u32 bg;
    extern u32 surface;
    extern u32 surfaceAlt;
    extern u32 overlay;

    // Dynamic accents
    extern u32 accent;
    extern u32 accentDim;
    extern u32 good;
    extern u32 warn;
    extern u32 danger;

    // Dynamic text
    extern u32 text;
    extern u32 textDim;
    extern u32 textOnAccent;

    // Metrics
    constexpr float pad       = 10.0f;
    constexpr float radius    = 8.0f;
    constexpr float headerH   = 28.0f;
    constexpr float hintH     = 20.0f;

    void apply(Type t);
    const char* name(Type t);
}

