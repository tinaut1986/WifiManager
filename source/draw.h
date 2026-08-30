#pragma once
#include <citro2d.h>
#include <string>

// -----------------------------------------------------------------------------
//  Thin citro2d drawing layer: scene setup, rounded panels, cached text.
// -----------------------------------------------------------------------------

namespace draw
{
    bool init();
    void exit();

    void frameBegin();               // clears both screens with theme::bg
    void sceneTop();
    void sceneBottom();
    void frameEnd();

    C3D_RenderTarget* topTarget();
    C3D_RenderTarget* bottomTarget();

    // Primitives -------------------------------------------------------------
    void rect(float x, float y, float w, float h, u32 color);
    void panel(float x, float y, float w, float h, u32 color, float r = 8.0f);
    void panelOutline(float x, float y, float w, float h, u32 color, float thickness = 2.0f, float r = 8.0f);
    void circle(float cx, float cy, float r, u32 color);

    // Text -----------------------------------------------------------------
    enum Align { AlignLeft, AlignCenter, AlignRight };

    float textWidth(const std::string& s, float scale);
    float textHeight(float scale);
    void  text(float x, float y, float scale, u32 color, const std::string& s, Align align = AlignLeft);
    // Truncated text with ellipsis if it exceeds maxWidth.
    void  textTruncated(float x, float y, float scale, u32 color, const std::string& s, float maxWidth, Align align = AlignLeft);
    // Wrapped text, returns height consumed. If maxLines > 0, stops after maxLines.
    float textWrapped(float x, float y, float scale, u32 color, const std::string& s, float maxWidth, int maxLines = 0);
    // Fit text scale between minScale and maxScale to fit in maxWidth.
    float textFitScale(const std::string& s, float maxScale, float minScale, float maxWidth);

    // Icons ----------------------------------------------------------------
    void iconQr(float cx, float cy, float size, u32 color, u32 bgColor);
    void iconGear(float cx, float cy, float radius, u32 color, u32 bgColor);
}



