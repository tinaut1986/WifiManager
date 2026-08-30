#include "draw.h"
#include "theme.h"
#include <vector>
#include <sstream>

namespace
{
    C3D_RenderTarget* s_top    = nullptr;
    C3D_RenderTarget* s_bottom = nullptr;
    C2D_TextBuf       s_buf    = nullptr;
    C2D_Font          s_font   = nullptr;
}

namespace draw
{

bool init()
{
    gfxInitDefault();
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) return false;
    C2D_Prepare();

    s_top    = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    s_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    s_buf    = C2D_TextBufNew(8192);
    s_font   = C2D_FontLoadSystem(CFG_REGION_USA);
    return s_top && s_bottom && s_buf;
}

void exit()
{
    if (s_font) C2D_FontFree(s_font);
    if (s_buf)  C2D_TextBufDelete(s_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

C3D_RenderTarget* topTarget()    { return s_top; }
C3D_RenderTarget* bottomTarget() { return s_bottom; }

void frameBegin()
{
    C2D_TextBufClear(s_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(s_top, theme::bg);
    C2D_TargetClear(s_bottom, theme::bg);
}

void sceneTop()    { C2D_SceneBegin(s_top); }
void sceneBottom() { C2D_SceneBegin(s_bottom); }
void frameEnd()    { C3D_FrameEnd(0); }

void rect(float x, float y, float w, float h, u32 color)
{
    C2D_DrawRectSolid(x, y, 0.0f, w, h, color);
}

void circle(float cx, float cy, float r, u32 color)
{
    C2D_DrawCircleSolid(cx, cy, 0.0f, r, color);
}

void panel(float x, float y, float w, float h, u32 color, float r)
{
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    rect(x + r, y, w - 2 * r, h, color);
    rect(x, y + r, w, h - 2 * r, color);
    circle(x + r,     y + r,     r, color);
    circle(x + w - r, y + r,     r, color);
    circle(x + r,     y + h - r, r, color);
    circle(x + w - r, y + h - r, r, color);
}

void panelOutline(float x, float y, float w, float h, u32 color, float t, float r)
{
    // cheap outline: draw a slightly bigger panel behind, caller draws fill on top
    panel(x - t, y - t, w + 2 * t, h + 2 * t, color, r + t);
}

// -----------------------------------------------------------------------------

static C2D_Text parse(const std::string& s)
{
    C2D_Text t;
    C2D_TextFontParse(&t, s_font, s_buf, s.c_str());
    C2D_TextOptimize(&t);
    return t;
}

float textWidth(const std::string& s, float scale)
{
    C2D_Text t = parse(s);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

float textHeight(float scale)
{
    C2D_Text t = parse("Ag");
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return h;
}

void text(float x, float y, float scale, u32 color, const std::string& s, Align align)
{
    C2D_Text t = parse(s);
    u32 flags = C2D_WithColor;
    if (align == AlignCenter) { flags |= C2D_AlignCenter; }
    else if (align == AlignRight) { flags |= C2D_AlignRight; }
    C2D_DrawText(&t, flags, x, y, 0.0f, scale, scale, color);
}

float textWrapped(float x, float y, float scale, u32 color, const std::string& s, float maxWidth)
{
    std::istringstream iss(s);
    std::string word, line;
    float lh = textHeight(scale) + 2.0f;
    float cy = y;

    auto flush = [&]() {
        if (!line.empty()) { text(x, cy, scale, color, line); cy += lh; line.clear(); }
    };

    while (iss >> word)
    {
        std::string candidate = line.empty() ? word : line + " " + word;
        if (textWidth(candidate, scale) > maxWidth && !line.empty())
        {
            flush();
            line = word;
        }
        else
        {
            line = candidate;
        }
    }
    flush();
    return cy - y;
}

} // namespace draw
