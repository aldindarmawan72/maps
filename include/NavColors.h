#ifndef NAV_COLORS_H
#define NAV_COLORS_H

#include <Arduino.h>

// ============================================================================
// Navigation UI colour system
// ----------------------------------------------------------------------------
// Layer 1: primitive scales (never use these directly in UI code)
// Layer 2: semantic tokens (use these in the UI)
// The palette is a dark "smart map" HUD: deep navy surfaces, Google-blue
// accent for the manoeuvre, amber for distance, muted slate for metadata.
// ============================================================================

namespace NavColors {
    // ---- primitives: ink (navy surfaces) ----
    static constexpr uint32_t Ink950 = 0x060A12;   // screen base
    static constexpr uint32_t Ink900 = 0x0B1220;   // card surface
    static constexpr uint32_t Ink800 = 0x111C2E;   // raised surface
    static constexpr uint32_t Ink700 = 0x1B2A42;   // borders / dividers

    // ---- primitives: slate (text) ----
    static constexpr uint32_t Slate100 = 0xFFFFFF; // primary text
    static constexpr uint32_t Slate300 = 0xC3CEDF; // secondary text
    static constexpr uint32_t Slate400 = 0x93A3BA; // tertiary text

    // ---- primitives: accents ----
    static constexpr uint32_t Blue500 = 0x1A73E8;  // manoeuvre accent
    static constexpr uint32_t Blue400 = 0x4C8DF6;  // accent highlight
    static constexpr uint32_t Amber400 = 0xF5B301; // distance accent
    static constexpr uint32_t Amber950 = 0x1A1200; // text on amber
    static constexpr uint32_t Green400 = 0x34D399; // connected / idle status

    // ---- semantic tokens ----
    static constexpr uint32_t BackgroundHex    = Ink950;
    static constexpr uint32_t SurfaceHex       = Ink900;
    static constexpr uint32_t SurfaceRaisedHex = Ink800;
    static constexpr uint32_t BorderHex        = Ink700;

    static constexpr uint32_t TextPrimaryHex   = Slate100;
    static constexpr uint32_t TextMutedHex     = Slate300;
    static constexpr uint32_t TextFaintHex     = Slate400;

    static constexpr uint32_t AccentHex        = Blue500;
    static constexpr uint32_t AccentSoftHex    = Blue400;
    static constexpr uint32_t DistanceHex      = Amber400;
    static constexpr uint32_t DistanceTextHex  = Amber950;
    static constexpr uint32_t SuccessHex       = Green400;

    // ---- BLE connection status dot ----
    static constexpr uint32_t BleConnectedHex    = 0x00E676; // connected
    static constexpr uint32_t BleDisconnectedHex = 0xFF3B30; // disconnected

    // ---- legacy aliases (used by the old LovyanGFX screen) ----
    static constexpr uint32_t ForegroundHex    = Slate100;
    static constexpr uint32_t SecondaryHex     = Blue500;
    static constexpr uint32_t AttentionHex     = 0xFF4D4F;
    static constexpr uint32_t InfoHex          = Amber400;

    static constexpr uint16_t Background       = 0x0000;
    static constexpr uint16_t Foreground       = 0xFFFF;
    static constexpr uint16_t Secondary        = 0x001F;
    static constexpr uint16_t Attention        = 0xF800;
    static constexpr uint16_t Info             = 0xFFE0;
}

#endif // NAV_COLORS_H
