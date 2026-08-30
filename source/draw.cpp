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
    if (s.empty()) return;
    C2D_Text t = parse(s);
    u32 flags = C2D_WithColor;
    if (align == AlignCenter) { flags |= C2D_AlignCenter; }
    else if (align == AlignRight) { flags |= C2D_AlignRight; }
    C2D_DrawText(&t, flags, x, y, 0.0f, scale, scale, color);
}

void textTruncated(float x, float y, float scale, u32 color, const std::string& s, float maxWidth, Align align)
{
    if (s.empty()) return;
    if (maxWidth <= 0.0f) return;

    if (textWidth(s, scale) <= maxWidth)
    {
        text(x, y, scale, color, s, align);
        return;
    }

    const std::string ellipsis = "...";
    float ellW = textWidth(ellipsis, scale);
    if (ellW >= maxWidth)
    {
        text(x, y, scale, color, ".", align);
        return;
    }

    std::string truncated = s;
    while (!truncated.empty())
    {
        truncated.pop_back();
        if (textWidth(truncated, scale) + ellW <= maxWidth)
        {
            text(x, y, scale, color, truncated + ellipsis, align);
            return;
        }
    }
    text(x, y, scale, color, ellipsis, align);
}

float textFitScale(const std::string& s, float maxScale, float minScale, float maxWidth)
{
    if (s.empty() || maxWidth <= 0.0f) return maxScale;
    float currentWidth = textWidth(s, maxScale);
    if (currentWidth <= maxWidth) return maxScale;

    float factor = maxWidth / currentWidth;
    float scaled = maxScale * factor;
    if (scaled < minScale) return minScale;
    return scaled;
}

static std::vector<std::string> breakParagraphToLines(const std::string& para, float scale, float maxWidth)
{
    std::vector<std::string> lines;
    if (maxWidth <= 0.0f) return lines;

    std::string currentLine;
    std::istringstream iss(para);
    std::string word;

    auto flushLine = [&]() {
        if (!currentLine.empty())
        {
            lines.push_back(currentLine);
            currentLine.clear();
        }
    };

    while (iss >> word)
    {
        std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
        if (textWidth(candidate, scale) <= maxWidth)
        {
            currentLine = candidate;
        }
        else
        {
            // Candidate doesn't fit
            if (!currentLine.empty())
            {
                flushLine();
            }

            // If the word itself fits on a new line
            if (textWidth(word, scale) <= maxWidth)
            {
                currentLine = word;
            }
            else
            {
                // Word is wider than maxWidth (e.g. continuous key or long URI) -> break by character
                for (size_t i = 0; i < word.size(); ++i)
                {
                    std::string nextCandidate = currentLine + word[i];
                    if (textWidth(nextCandidate, scale) <= maxWidth || currentLine.empty())
                    {
                        currentLine = nextCandidate;
                    }
                    else
                    {
                        flushLine();
                        currentLine = std::string(1, word[i]);
                    }
                }
            }
        }
    }

    flushLine();
    return lines;
}

float textWrapped(float x, float y, float scale, u32 color, const std::string& s, float maxWidth, int maxLines)
{
    if (s.empty() || maxWidth <= 0.0f) return 0.0f;

    float lh = textHeight(scale) + 2.0f;
    float cy = y;
    int lineCount = 0;

    std::istringstream iss(s);
    std::string para;

    while (std::getline(iss, para))
    {
        if (para.empty())
        {
            // Empty line / newline
            cy += lh;
            lineCount++;
            if (maxLines > 0 && lineCount >= maxLines) break;
            continue;
        }

        std::vector<std::string> lines = breakParagraphToLines(para, scale, maxWidth);
        for (const auto& l : lines)
        {
            text(x, cy, scale, color, l);
            cy += lh;
            lineCount++;
            if (maxLines > 0 && lineCount >= maxLines) break;
        }
        if (maxLines > 0 && lineCount >= maxLines) break;
    }

    return cy - y;
}

void iconQr(float cx, float cy, float size, u32 color, u32 bgColor)
{
    // 13x13 authentic QR icon bitmap
    static const u16 qrPattern[13] = {
        0b1111101011111, // row 0: [#####] . # . [#####]
        0b1000100010001, // row 1: [#...#] . . . [#...#]
        0b1010101010101, // row 2: [#.###] . # . [#.###]
        0b1000100010001, // row 3: [#...#] . . . [#...#]
        0b1111101011111, // row 4: [#####] . # . [#####]
        0b0000000000000, // row 5: ....... . . . .......
        0b1010101010101, // row 6: #.#.#.# . # . #.#.#.#
        0b0000000000000, // row 7: ....... . . . .......
        0b1111101010101, // row 8: [#####] . # . # . # .
        0b1000100001110, // row 9: [#...#] . . . . ### .
        0b1010101011011, // row 10:[#.###] . # . ## . ##
        0b1000100001101, // row 11:[#...#] . . . . ## .#
        0b1111101010111, // row 12:[#####] . # . # . ###
    };

    const int N = 13;
    float m = size / static_cast<float>(N);
    float x0 = cx - size * 0.5f;
    float y0 = cy - size * 0.5f;

    for (int r = 0; r < N; ++r)
    {
        for (int c = 0; c < N; ++c)
        {
            if ((qrPattern[r] >> (N - 1 - c)) & 1)
            {
                rect(x0 + c * m, y0 + r * m, m, m, color);
            }
        }
    }
}

void iconGear(float cx, float cy, float radius, u32 color, u32 bgColor)
{
    float toothR = radius * 0.30f;
    float toothDist = radius * 0.68f;
    for (int i = 0; i < 6; ++i)
    {
        float angle = static_cast<float>(i) * (3.1415926535f / 3.0f);
        float tx = cx + toothDist * std::cos(angle);
        float ty = cy + toothDist * std::sin(angle);
        circle(tx, ty, toothR, color);
    }
    circle(cx, cy, radius * 0.70f, color);
    circle(cx, cy, radius * 0.28f, bgColor);
}

} // namespace draw


