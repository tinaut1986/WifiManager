#include "app.h"

#include <3ds.h>
#include <vector>
#include <string>

#include "draw.h"
#include "theme.h"
#include "lang.h"
#include "config.h"
#include "wifi.h"
#include "wifiqr.h"
#include "qrscan.h"
#include "qrgen.h"
#include "kbd.h"

extern "C" {
#include "file.h"
}

extern FS_Archive sdmcArchive;

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

using draw::AlignCenter;
using draw::AlignRight;

namespace
{
constexpr float FS_HEADER = 0.50f;
constexpr float FS_TITLE  = 0.50f;
constexpr float FS_BODY   = 0.42f;
constexpr float FS_SMALL  = 0.38f;
constexpr float FS_HINT   = 0.38f;

constexpr u32 QR_LIGHT = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
constexpr u32 QR_DARK  = C2D_Color32(0x00, 0x00, 0x00, 0xFF);

// -----------------------------------------------------------------------------
//  State
// -----------------------------------------------------------------------------
struct State
{
    wifi::Slot  console[CFG_WIFI_SLOTS];
    bool        consoleOk[CFG_WIFI_SLOTS] = {false, false, false};
    std::vector<wifi::Backup> backups;

    int  selSlot = 0;
    int  selRow  = 0;   // index into backups
    int  scroll  = 0;
    bool showPw  = false;

    std::string toast;
    int         toastTimer = 0;
    u32         toastColor  = theme::accent;
} S;

const int kListVisible = 5;

int  rowCount() { return static_cast<int>(S.backups.size()); }
wifi::Backup* selBackup()
{
    if (S.selRow >= 0 && S.selRow < static_cast<int>(S.backups.size()))
        return &S.backups[S.selRow];
    return nullptr;
}

void clampSelRow()
{
    int n = static_cast<int>(S.backups.size());
    if (n == 0)
    {
        S.selRow = 0;
        S.scroll = 0;
    }
    else
    {
        if (S.selRow >= n) S.selRow = n - 1;
        if (S.selRow < 0)  S.selRow = 0;
        if (S.selRow < S.scroll) S.scroll = S.selRow;
        if (S.selRow >= S.scroll + kListVisible) S.scroll = S.selRow - kListVisible + 1;
        int maxScroll = std::max(0, n - kListVisible);
        if (S.scroll > maxScroll) S.scroll = maxScroll;
        if (S.scroll < 0) S.scroll = 0;
    }
}

void refreshConsole()
{
    for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
        S.consoleOk[i] = wifi::readConsoleSlot(i, S.console[i]);
}
void refreshBackups()
{
    S.backups = wifi::listBackups();
    clampSelRow();
}

void toast(const std::string& m, u32 c = theme::accent)
{
    S.toast = m; S.toastTimer = 150; S.toastColor = c;
}

u32 securityColor(int enc)
{
    switch (enc)
    {
        case WIFI_ENCRYPTION_OPEN:          return theme::danger;
        case WIFI_ENCRYPTION_WPA2_PSK_AES:
        case WIFI_ENCRYPTION_WPA2_PSK_TKIP: return theme::good;
        default:                            return theme::warn;
    }
}

const char* secName(wifi::Security s)
{
    switch (s)
    {
        case wifi::Security::Open: return lang::get(lang::STR_SEC_OPEN);
        case wifi::Security::WEP:  return lang::get(lang::STR_SEC_WEP);
        case wifi::Security::WPA:  return lang::get(lang::STR_SEC_WPA);
        default:                   return lang::get(lang::STR_SEC_WPA2);
    }
}

// -----------------------------------------------------------------------------
//  Shared chrome
// -----------------------------------------------------------------------------
void drawHeader(const std::string& title, const std::string& right = "")
{
    draw::rect(0, 0, 400, theme::headerH, theme::surface);
    draw::rect(0, theme::headerH, 400, 2, theme::accent);
    float rightW = right.empty() ? 0.0f : (draw::textWidth(right, FS_SMALL) + 12.0f);
    draw::textTruncated(theme::pad, 7, FS_HEADER, theme::text, title, 400 - theme::pad * 2 - rightW);
    if (!right.empty())
        draw::text(400 - theme::pad, 8, FS_SMALL, theme::textDim, right, AlignRight);
}

void drawHint(bool bottom, const std::string& hint)
{
    float w = bottom ? 320 : 400;
    float y = 240 - theme::hintH;
    draw::rect(0, y, w, theme::hintH, theme::surface);
    draw::textTruncated(theme::pad, y + 4, FS_HINT, theme::textDim, hint, w - theme::pad * 2);
}

void drawToast()
{
    if (S.toastTimer <= 0) return;
    S.toastTimer--;
    float maxTw = 380.0f;
    float tw = draw::textWidth(S.toast, FS_BODY) + 24.0f;
    if (tw > maxTw) tw = maxTw;
    float x = (400 - tw) / 2;
    draw::panel(x, 198, tw, 24, theme::surfaceAlt, 12);
    draw::rect(x, 198, 3, 24, S.toastColor);
    draw::textTruncated(x + 14, 204, FS_BODY, theme::text, S.toast, tw - 24);
}

void drawBadge(float x, float y, const std::string& s, u32 color)
{
    float w = draw::textWidth(s, FS_SMALL) + 12;
    draw::panel(x, y, w, 15, color, 7);
    draw::text(x + 6, y + 2, FS_SMALL, theme::textOnAccent, s);
}

// Blocking yes/no modal.
bool confirm(const std::string& title, const std::string& body)
{
    while (aptMainLoop())
    {
        hidScanInput();
        u32 k = hidKeysDown();
        touchPosition touch;
        hidTouchRead(&touch);

        float bw = 340, bh = 145, bx = 30, by = 47;

        if (k & KEY_A) return true;
        if (k & KEY_B) return false;
        if (k & KEY_TOUCH)
        {
            if (touch.py >= by + bh - 36 && touch.py <= by + bh)
            {
                if (touch.px >= bx && touch.px < bx + bw / 2) return true;
                if (touch.px >= bx + bw / 2 && touch.px <= bx + bw) return false;
            }
            else if (touch.px < bx || touch.px > bx + bw || touch.py < by || touch.py > by + bh)
            {
                return false;
            }
        }

        draw::frameBegin();
        draw::sceneTop();
        draw::rect(0, 0, 400, 240, theme::overlay);
        draw::panel(bx, by, bw, bh, theme::surface, theme::radius);
        draw::rect(bx, by, bw, 3, theme::accent);
        draw::textTruncated(bx + bw / 2, by + 12, FS_TITLE, theme::text, title, bw - 32, AlignCenter);
        draw::textWrapped(bx + 16, by + 36, FS_BODY, theme::text, body, bw - 32);
        
        // Buttons bar
        draw::panel(bx + 16, by + bh - 28, bw / 2 - 24, 22, theme::accentDim, 5);
        draw::text(bx + 16 + (bw / 2 - 24) / 2, by + bh - 24, FS_SMALL, theme::text, lang::get(lang::STR_BTN_CONFIRM), AlignCenter);

        draw::panel(bx + bw / 2 + 8, by + bh - 28, bw / 2 - 24, 22, theme::surfaceAlt, 5);
        draw::text(bx + bw / 2 + 8 + (bw / 2 - 24) / 2, by + bh - 24, FS_SMALL, theme::textDim, lang::get(lang::STR_BTN_CANCEL), AlignCenter);

        draw::sceneBottom();
        draw::frameEnd();
    }
    return false;
}

// Blocking option picker. Returns the chosen index, or -1 on cancel.
int menu(const std::string& title, const std::vector<std::string>& opts)
{
    int sel = 0;
    while (aptMainLoop())
    {
        hidScanInput();
        u32 k = hidKeysDown();
        touchPosition touch;
        hidTouchRead(&touch);

        int n = (int)opts.size();
        if (k & KEY_B) return -1;
        if (k & KEY_DDOWN) sel = (sel + 1) % n;
        if (k & KEY_DUP)   sel = (sel + n - 1) % n;
        if (k & KEY_A) return sel;

        float rowH = 26;
        float bh = 58 + rowH * opts.size();
        float bw = 300, bx = 50, by = (240 - bh) / 2;

        if (k & KEY_TOUCH)
        {
            if (touch.px >= bx + 8 && touch.px <= bx + bw - 8)
            {
                for (size_t i = 0; i < opts.size(); ++i)
                {
                    float ry = by + 34 + i * rowH;
                    if (touch.py >= ry - 1 && touch.py <= ry + rowH - 1)
                    {
                        return (int)i;
                    }
                }
            }
            else if (touch.px < bx || touch.px > bx + bw || touch.py < by || touch.py > by + bh)
            {
                return -1;
            }
        }

        draw::frameBegin();
        draw::sceneTop();
        draw::rect(0, 0, 400, 240, theme::overlay);
        draw::panel(bx, by, bw, bh, theme::surface, theme::radius);
        draw::rect(bx, by, bw, 3, theme::accent);
        draw::textTruncated(bx + 16, by + 12, FS_TITLE, theme::text, title, bw - 32);
        for (size_t i = 0; i < opts.size(); ++i)
        {
            float ry = by + 34 + i * rowH;
            if ((int)i == sel)
                draw::panel(bx + 8, ry - 1, bw - 16, rowH - 2, theme::accentDim, 6);
            draw::textTruncated(bx + 20, ry + 3, FS_BODY, theme::text, opts[i], bw - 36);
        }
        draw::text(bx + bw / 2, by + bh - 16, FS_SMALL, theme::textDim,
                   lang::get(lang::STR_MODAL_CHOOSE), AlignCenter);
        draw::sceneBottom();
        draw::frameEnd();
    }
    return -1;
}

// -----------------------------------------------------------------------------
//  Credential editor (used for QR import and for editing a saved backup).
//  Returns true when the user confirms with X.
// -----------------------------------------------------------------------------
bool runEditor(wifi::Credentials& cred, const std::string& title)
{
    int field = 0;                 // 0 ssid, 1 security, 2 password
    const int nFields = 3;

    while (aptMainLoop())
    {
        hidScanInput();
        u32 k = hidKeysDown();

        if (k & KEY_B) return false;
        if (k & KEY_X) return true;
        if (k & KEY_DDOWN) field = (field + 1) % nFields;
        if (k & KEY_DUP)   field = (field + nFields - 1) % nFields;

        if ((k & (KEY_DLEFT | KEY_DRIGHT)) && field == 1)
        {
            int dir = (k & KEY_DRIGHT) ? 1 : -1;
            cred.security = static_cast<wifi::Security>(
                (static_cast<int>(cred.security) + dir + 4) % 4);
        }

        if (k & KEY_A)
        {
            std::string v;
            if (field == 0 && kbd::text(v, lang::get(lang::STR_PROMPT_SSID), cred.ssid, 32))
                cred.ssid = v;
            else if (field == 2 && kbd::text(v, lang::get(lang::STR_PROMPT_PW), cred.password, 63, false, true))
                cred.password = v;
        }

        draw::frameBegin();
        draw::sceneTop();
        drawHeader(title);

        float x = theme::pad, y = theme::headerH + 18, w = 400 - x * 2;
        auto row = [&](int idx, const std::string& name, const std::string& val)
        {
            float rh = 34;
            if (idx == field) draw::panel(x, y, w, rh, theme::accentDim, 6);
            draw::text(x + 10, y + 4, FS_SMALL, theme::textDim, name);
            draw::textTruncated(x + 10, y + 17, FS_BODY, theme::text, val, w - 20);
            y += rh + 6;
        };
        row(0, lang::get(lang::STR_EDITOR_SSID_HINT), cred.ssid.empty() ? lang::get(lang::STR_NONE) : cred.ssid);
        row(1, lang::get(lang::STR_EDITOR_SEC_HINT), secName(cred.security));
        row(2, lang::get(lang::STR_EDITOR_PW_HINT),
            cred.security == wifi::Security::Open ? lang::get(lang::STR_NO_PASSWORD)
            : cred.password.empty() ? lang::get(lang::STR_NONE) : cred.password);

        drawHint(false, lang::get(lang::STR_EDITOR_HINT_TOP));
        draw::sceneBottom();
        drawHint(true, lang::get(lang::STR_EDITOR_HINT_BOT));
        draw::frameEnd();
    }
    return false;
}

// -----------------------------------------------------------------------------
//  Share screen: render a Wi-Fi QR code from credentials.
// -----------------------------------------------------------------------------
void runShareQr(const wifi::Credentials& cred, const std::string& subtitle)
{
    std::string uri = wifiqr::encode(cred);
    qrgen::Code code;
    bool ok = qrgen::make(uri, code);

    while (aptMainLoop())
    {
        hidScanInput();
        if (hidKeysDown() & KEY_B) return;

        draw::frameBegin();
        draw::sceneTop();
        drawHeader(lang::get(lang::STR_SHARE_TITLE), subtitle);

        if (ok && code.size > 0)
        {
            float avail = 240 - theme::headerH - 20;
            int scale = (int)(avail / code.size);
            if (scale < 1) scale = 1;
            if (scale > 6) scale = 6;
            float dim = (float)scale * code.size;
            float ox = (400 - dim) / 2;
            float oy = theme::headerH + (avail - dim) / 2 + 12;

            draw::panel(ox - 10, oy - 10, dim + 20, dim + 20, QR_LIGHT, 4);
            for (int yy = 0; yy < code.size; ++yy)
                for (int xx = 0; xx < code.size; ++xx)
                    if (code.at(xx, yy))
                        draw::rect(ox + xx * scale, oy + yy * scale, scale, scale, QR_DARK);
        }
        else
        {
            draw::text(200, 110, FS_BODY, theme::danger,
                       lang::get(lang::STR_SHARE_ERR), AlignCenter);
        }

        draw::sceneBottom();
        drawHeader("");
        float y = theme::headerH + 12;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, "SSID");
        draw::text(theme::pad, y + 13, FS_BODY, theme::text,
                   cred.ssid.empty() ? lang::get(lang::STR_NONE) : cred.ssid);
        y += 40;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, lang::get(lang::STR_DETAILS_SEC_PROTO));
        draw::text(theme::pad, y + 13, FS_BODY, theme::text, secName(cred.security));
        y += 40;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, lang::get(lang::STR_SHARE_PAYLOAD));
        draw::textWrapped(theme::pad, y + 13, FS_SMALL, theme::textDim, uri, 320 - theme::pad * 2);

        drawHint(true, lang::get(lang::STR_HINT_BACK));
        draw::frameEnd();
    }
}

// -----------------------------------------------------------------------------
//  Touch interaction state
// -----------------------------------------------------------------------------
struct TouchTracker
{
    bool isDown = false;
    bool isDraggingList = false;
    bool isDraggingScrollbar = false;
    touchPosition startPos{};
    touchPosition lastPos{};
    int startScroll = 0;
} g_touch;

// -----------------------------------------------------------------------------
//  Slot detail card (top screen)
// -----------------------------------------------------------------------------
void drawSlotCard(float x, float y, float w, float h,
                  const std::string& heading, const std::string& sub,
                  bool exists, const wifi::Slot* slot, bool highlight)
{
    if (highlight)
        draw::panel(x - 3, y - 3, w + 6, h + 6, theme::accent, theme::radius + 3);
    draw::panel(x, y, w, h, theme::surface, theme::radius);

    draw::textTruncated(x + 12, y + 9, FS_BODY, theme::textDim, heading, w - 24);
    float ty = y + 26;
    if (!sub.empty()) { draw::textTruncated(x + 12, ty, FS_SMALL, theme::textDim, sub, w - 24); ty += 15; }

    if (!exists || !slot)
    {
        draw::text(x + 12, y + h / 2, FS_BODY, theme::textDim, "- empty -");
        return;
    }

    ty += 6;
    float ssidScale = draw::textFitScale(slot->ssid, FS_TITLE, 0.36f, w - 24);
    draw::textTruncated(x + 12, ty, ssidScale, theme::text,
                        slot->ssid.empty() ? "(no SSID)" : slot->ssid, w - 24);
    ty += 22;
    drawBadge(x + 12, ty, wifi::securityLabel(slot->encryption),
              securityColor(slot->encryption));
    ty += 22;

    std::string pw = slot->encryption == WIFI_ENCRYPTION_OPEN
                         ? lang::get(lang::STR_NO_PASSWORD)
                         : (S.showPw ? slot->password : std::string(slot->password.size(), '*'));
    draw::text(x + 12, ty, FS_SMALL, theme::textDim, lang::get(lang::STR_PASSWORD));
    float pwScale = (pw.size() > 24) ? FS_SMALL : FS_BODY;
    draw::textWrapped(x + 12, ty + 14, pwScale, theme::text, pw, w - 24, 4);
}

void drawHome()
{
    draw::sceneTop();
    drawHeader(lang::get(lang::STR_TITLE), lang::get(lang::STR_SELECT_HELP));

    float cardY = theme::headerH + 10;
    float cardH = 240 - cardY - theme::hintH - 10;
    char slotTitleBuf[64];
    std::snprintf(slotTitleBuf, sizeof(slotTitleBuf), lang::get(lang::STR_SLOT_TITLE), S.selSlot + 1);

    drawSlotCard(theme::pad, cardY, 185, cardH,
                 slotTitleBuf, "",
                 S.consoleOk[S.selSlot] && S.console[S.selSlot].exists,
                 &S.console[S.selSlot], true);

    wifi::Backup* b = selBackup();
    std::string dateStr;
    if (b)
    {
        if (b->savedDate.empty())
        {
            dateStr = lang::get(lang::STR_SAVED_DATE_UNKNOWN);
        }
        else
        {
            char dBuf[96];
            std::snprintf(dBuf, sizeof(dBuf), lang::get(lang::STR_SAVED_DATE), b->savedDate.c_str());
            dateStr = dBuf;
        }
    }

    drawSlotCard(205, cardY, 185, cardH,
                 b ? b->displayName : lang::get(lang::STR_BACKUP_TITLE),
                 dateStr,
                 b != nullptr, b ? &b->slot : nullptr, false);

    drawHint(false, lang::get(lang::STR_HINT_TOP));
    drawToast();

    draw::sceneBottom();
    float rowY = 8, rowH = 38;
    float slotW = 62, slotGap = 6;

    // 3 Slot buttons
    for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
    {
        float sx = theme::pad + i * (slotW + slotGap);
        if (i == S.selSlot) draw::panel(sx - 2, rowY - 2, slotW + 4, rowH + 4, theme::accent, 8);
        draw::panel(sx, rowY, slotW, rowH, theme::surface, 8);
        draw::text(sx + slotW / 2, rowY + 4, FS_BODY, theme::text,
                   "Slot " + std::to_string(i + 1), AlignCenter);
        bool ok = S.consoleOk[i] && S.console[i].exists;
        draw::text(sx + slotW / 2, rowY + 20, FS_SMALL,
                   ok ? theme::good : theme::textDim,
                   ok ? lang::get(lang::STR_SLOT_SAVED) : lang::get(lang::STR_SLOT_EMPTY), AlignCenter);
    }

    // Small square QR button
    float qrX = theme::pad + 3 * (slotW + slotGap) + 4;
    float qrW = 38;
    draw::panel(qrX, rowY, qrW, rowH, theme::surface, 8);
    draw::panelOutline(qrX, rowY, qrW, rowH, theme::accentDim, 1.0f, 8);
    draw::iconQr(qrX + qrW * 0.5f, rowY + rowH * 0.5f, 20.0f, theme::accent, theme::surface);

    // Small square Settings gear button
    float setX = qrX + qrW + 6;
    float setW = 38;
    draw::panel(setX, rowY, setW, rowH, theme::surface, 8);
    draw::panelOutline(setX, rowY, setW, rowH, theme::accentDim, 1.0f, 8);
    draw::iconGear(setX + setW * 0.5f, rowY + rowH * 0.5f, 10.0f, theme::accent, theme::surface);

    // Backups List Panel
    float listY = rowY + rowH + 8;
    float listH = 240 - listY - theme::hintH - 6;
    float listW = 320 - theme::pad * 2;
    draw::panel(theme::pad, listY, listW, listH, theme::surface, 8);

    int totalBackups = static_cast<int>(S.backups.size());

    if (totalBackups == 0)
    {
        draw::text(160, listY + 54, FS_BODY, theme::textDim, lang::get(lang::STR_BACKUPS_NONE), AlignCenter);
        char pressRBuf[96];
        std::snprintf(pressRBuf, sizeof(pressRBuf), lang::get(lang::STR_BACKUP_PRESS_R), S.selSlot + 1);
        draw::text(160, listY + 76, FS_SMALL, theme::accent, pressRBuf, AlignCenter);
    }
    else
    {
        float itemH = listH / static_cast<float>(kListVisible);
        float sbW = 5.0f;
        float sbX = theme::pad + listW - sbW - 3.0f;
        float sbY = listY + 4.0f;
        float sbH = listH - 8.0f;

        // Scrollbar track
        draw::rect(sbX, sbY, sbW, sbH, theme::surfaceAlt);

        // Scrollbar thumb
        int maxScroll = std::max(0, totalBackups - kListVisible);
        float thumbH = totalBackups > kListVisible
                           ? std::max(16.0f, sbH * (static_cast<float>(kListVisible) / static_cast<float>(totalBackups)))
                           : sbH;
        float thumbY = maxScroll > 0
                           ? sbY + (sbH - thumbH) * (static_cast<float>(S.scroll) / static_cast<float>(maxScroll))
                           : sbY;
        draw::panel(sbX, thumbY, sbW, thumbH, theme::accent, 2.5f);

        // Render visible backup items
        for (int v = 0; v < kListVisible; ++v)
        {
            int r = S.scroll + v;
            if (r >= totalBackups) break;
            float iy = listY + v * itemH;

            if (r == S.selRow)
                draw::panel(theme::pad + 3, iy + 2, listW - sbW - 10, itemH - 4,
                            theme::accentDim, 6);

            const wifi::Backup& bk = S.backups[r];
            float rightW = bk.savedDate.empty() ? 0.0f : (draw::textWidth(bk.savedDate, FS_SMALL) + 10.0f);
            float maxLabelW = (listW - sbW - 20.0f) - rightW;

            draw::textTruncated(theme::pad + 12, iy + itemH / 2 - 7, FS_BODY, theme::text, bk.displayName, maxLabelW);
            if (!bk.savedDate.empty())
                draw::text(sbX - 6, iy + itemH / 2 - 6, FS_SMALL,
                           theme::textDim, bk.savedDate, AlignRight);
        }
    }

    drawHint(true, lang::get(lang::STR_HINT_BOT));
}

// -----------------------------------------------------------------------------
//  Flows
// -----------------------------------------------------------------------------
void showSlotDetails(const std::string& heading, const wifi::Slot& slot)
{
    bool showPlain = true;
    while (aptMainLoop())
    {
        hidScanInput();
        u32 k = hidKeysDown();
        touchPosition touch;
        hidTouchRead(&touch);

        if (k & (KEY_B | KEY_A)) break;
        if (k & KEY_Y) showPlain = !showPlain;
        if (k & KEY_TOUCH)
        {
            if (touch.py > 100 && touch.py < 180) showPlain = !showPlain;
            else break;
        }

        draw::frameBegin();
        draw::sceneTop();
        drawHeader(lang::get(lang::STR_DETAILS_TITLE), slot.ssid);

        float bw = 380, bh = 175, bx = 10, by = theme::headerH + 10;
        draw::panel(bx, by, bw, bh, theme::surface, theme::radius);
        draw::rect(bx, by, bw, 3, theme::accent);

        draw::textTruncated(bx + 14, by + 10, FS_SMALL, theme::textDim, heading, bw - 28);

        float ssidScale = draw::textFitScale(slot.ssid, FS_TITLE, 0.40f, bw - 28);
        draw::textTruncated(bx + 14, by + 26, ssidScale, theme::text,
                            slot.ssid.empty() ? lang::get(lang::STR_NO_SSID) : slot.ssid, bw - 28);

        float badgeY = by + 48;
        drawBadge(bx + 14, badgeY, wifi::securityLabel(slot.encryption), securityColor(slot.encryption));

        float pwLabelY = badgeY + 24;
        std::string modeStr = (slot.encryption == WIFI_ENCRYPTION_OPEN) ? "" :
                              (showPlain ? std::string(" ") + lang::get(lang::STR_VISIBLE)
                                         : std::string(" ") + lang::get(lang::STR_MASKED));
        draw::text(bx + 14, pwLabelY, FS_SMALL, theme::textDim, std::string(lang::get(lang::STR_PASSWORD)) + modeStr + ":");

        float pwBoxY = pwLabelY + 16;
        float pwBoxH = bh - (pwBoxY - by) - 10;
        draw::panel(bx + 10, pwBoxY, bw - 20, pwBoxH, theme::surfaceAlt, 6);

        std::string pwText;
        if (slot.encryption == WIFI_ENCRYPTION_OPEN)
            pwText = lang::get(lang::STR_OPEN_NO_PASSWORD);
        else if (!showPlain)
            pwText = std::string(slot.password.size(), '*');
        else
            pwText = slot.password.empty() ? lang::get(lang::STR_NONE) : slot.password;

        float pwScale = (pwText.size() > 50) ? FS_SMALL : FS_BODY;
        draw::textWrapped(bx + 18, pwBoxY + 8, pwScale, theme::text, pwText, bw - 36);

        drawHint(false, "Y / Tap: password    B/A: back");

        draw::sceneBottom();
        drawHeader(lang::get(lang::STR_DETAILS_CONN_INFO));
        float y = theme::headerH + 12;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, lang::get(lang::STR_DETAILS_SEC_PROTO));
        draw::text(theme::pad, y + 13, FS_BODY, theme::text, wifi::securityLabel(slot.encryption));
        y += 36;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, lang::get(lang::STR_DETAILS_PW_LENGTH));
        char lenBuf[64];
        if (slot.encryption == WIFI_ENCRYPTION_OPEN)
            std::snprintf(lenBuf, sizeof(lenBuf), "%s", lang::get(lang::STR_DETAILS_OPEN_CHARS));
        else
            std::snprintf(lenBuf, sizeof(lenBuf), lang::get(lang::STR_DETAILS_CHARS), (int)slot.password.size());
        draw::text(theme::pad, y + 13, FS_BODY, theme::text, lenBuf);
        y += 36;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, lang::get(lang::STR_DETAILS_MODE));
        draw::text(theme::pad, y + 13, FS_BODY, theme::text,
                   slot.editableSecurity ? lang::get(lang::STR_DETAILS_MANUAL) : lang::get(lang::STR_DETAILS_WPS));
        y += 36;
        draw::textWrapped(theme::pad, y, FS_SMALL, theme::accent, lang::get(lang::STR_NOTE_24GHZ_ONLY), 320 - theme::pad * 2);

        drawHint(true, lang::get(lang::STR_HINT_TAP_BACK));
        draw::frameEnd();
    }
}

void saveSlotAsBackup(const wifi::Slot& slot)
{
    if (!slot.exists)
    {
        toast(lang::get(lang::STR_TOAST_SLOT_EMPTY), theme::warn);
        return;
    }
    std::string alias;
    if (!kbd::text(alias, lang::get(lang::STR_PROMPT_ALIAS), "", 96, false, true))
        return;

    std::string path = wifi::saveBackup(slot, alias);
    if (!path.empty())
    {
        refreshBackups();
        clampSelRow();
        if (alias.empty())
        {
            toast(lang::get(lang::STR_TOAST_BACKUP_SAVED), theme::good);
        }
        else
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), lang::get(lang::STR_TOAST_BACKUP_SAVED_ALIAS), alias.c_str());
            toast(buf, theme::good);
        }
    }
    else toast(lang::get(lang::STR_TOAST_BACKUP_FAILED), theme::danger);
}

void importCredentials(wifi::Credentials cred, bool fromUri)
{
    if (!runEditor(cred, fromUri ? lang::get(lang::STR_REVIEW_WIFI_QR) : lang::get(lang::STR_REVIEW_TEXT_QR)))
        return;

    char slot1Buf[64], slot2Buf[64], slot3Buf[64];
    std::snprintf(slot1Buf, sizeof(slot1Buf), lang::get(lang::STR_WRITE_TO_SLOT), 1);
    std::snprintf(slot2Buf, sizeof(slot2Buf), lang::get(lang::STR_WRITE_TO_SLOT), 2);
    std::snprintf(slot3Buf, sizeof(slot3Buf), lang::get(lang::STR_WRITE_TO_SLOT), 3);

    int choice = menu(lang::get(lang::STR_SAVE_WHERE),
                      {lang::get(lang::STR_SAVE_AS_BACKUP), slot1Buf, slot2Buf, slot3Buf});
    if (choice < 0) return;

    const wifi::Slot* templ = nullptr;
    for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
        if (S.consoleOk[i] && S.console[i].exists) { templ = &S.console[i]; break; }
    wifi::Slot built = wifi::fromCredentials(cred, templ);

    if (choice == 0) { saveSlotAsBackup(built); return; }

    int slot = choice - 1;
    char confTitle[64], confBody[160];
    std::snprintf(confTitle, sizeof(confTitle), lang::get(lang::STR_CONFIRM_APPLY_TITLE), slot + 1);
    std::snprintf(confBody, sizeof(confBody), lang::get(lang::STR_CONFIRM_APPLY_BODY), slot + 1, cred.ssid.c_str());

    if (confirm(confTitle, confBody))
    {
        if (R_SUCCEEDED(wifi::writeConsoleSlot(slot, built)))
        {
            refreshConsole();
            char okBuf[64];
            std::snprintf(okBuf, sizeof(okBuf), lang::get(lang::STR_TOAST_SLOT_WRITTEN), slot + 1);
            toast(okBuf, theme::good);
        }
        else toast("Write failed", theme::danger);
    }
}

void runScan()
{
    if (!qrscan::init()) { qrscan::exit(); toast("Camera unavailable", theme::danger); return; }

    std::string payload;
    bool got = false;

    while (aptMainLoop())
    {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & (KEY_B | KEY_TOUCH)) break;
        if (qrscan::failed()) { toast("Camera error", theme::danger); break; }
        if (qrscan::update(payload)) got = true;

        draw::frameBegin();
        draw::sceneTop();
        if (qrscan::hasPreview())
            C2D_DrawImageAt(qrscan::preview(), 0, 0, 0.0f, nullptr, 1.0f, 1.0f);
        else
            draw::text(200, 112, FS_BODY, theme::text, lang::get(lang::STR_SCAN_STARTING), AlignCenter);

        float sz = 150, cx = 200, cy = 120;
        u32 c = theme::accent;
        draw::rect(cx - sz / 2, cy - sz / 2, 24, 3, c);
        draw::rect(cx - sz / 2, cy - sz / 2, 3, 24, c);
        draw::rect(cx + sz / 2 - 24, cy - sz / 2, 24, 3, c);
        draw::rect(cx + sz / 2 - 3, cy - sz / 2, 3, 24, c);
        draw::rect(cx - sz / 2, cy + sz / 2 - 3, 24, 3, c);
        draw::rect(cx - sz / 2, cy + sz / 2 - 24, 3, 24, c);
        draw::rect(cx + sz / 2 - 24, cy + sz / 2 - 3, 24, 3, c);
        draw::rect(cx + sz / 2 - 3, cy + sz / 2 - 24, 3, 24, c);

        draw::sceneBottom();
        drawHeader(lang::get(lang::STR_SCAN_TITLE));
        draw::text(160, 44, FS_BODY, theme::text, lang::get(lang::STR_SCAN_POINT), AlignCenter);
        draw::text(160, 68, FS_SMALL, theme::textDim, lang::get(lang::STR_SCAN_SUB_1), AlignCenter);
        draw::text(160, 82, FS_SMALL, theme::textDim, lang::get(lang::STR_SCAN_SUB_2), AlignCenter);
        drawHint(true, lang::get(lang::STR_HINT_TAP_BACK));
        draw::frameEnd();

        if (got) break;
    }

    qrscan::exit();
    if (!got) return;

    wifiqr::ParseResult pr = wifiqr::parse(payload);
    if (!pr.ok) { toast(pr.error.empty() ? "Unrecognised QR code" : pr.error, theme::danger); return; }
    importCredentials(pr.cred, pr.fromStandardUri);
}

void runSettings()
{
    while (aptMainLoop())
    {
        hidScanInput();
        u32 k = hidKeysDown();
        touchPosition touch;
        hidTouchRead(&touch);

        if (k & (KEY_B | KEY_START)) break;

        draw::frameBegin();
        draw::sceneTop();
        drawHeader(lang::get(lang::STR_SETTINGS_TITLE));

        float cardW = 380, cardH = 175, cardX = 10, cardY = theme::headerH + 10;
        draw::panel(cardX, cardY, cardW, cardH, theme::surface, theme::radius);
        draw::rect(cardX, cardY, cardW, 3, theme::accent);

        draw::text(cardX + 16, cardY + 14, FS_TITLE, theme::text, lang::get(lang::STR_SETTINGS_APP_INFO));

        draw::text(cardX + 16, cardY + 38, FS_BODY, theme::accent, std::string("WiFiManager+ ") + APP_VERSION);
        draw::text(cardX + 16, cardY + 56, FS_SMALL, theme::textDim, "Nintendo 3DS Wi-Fi Profile & Backup Manager");

        draw::textWrapped(cardX + 16, cardY + 80, FS_SMALL, theme::warn, lang::get(lang::STR_NOTE_24GHZ_ONLY), cardW - 32);
        draw::textWrapped(cardX + 16, cardY + 120, FS_SMALL, theme::textDim, lang::get(lang::STR_NOTE_RELOAD_SYS), cardW - 32);

        drawHint(false, lang::get(lang::STR_HINT_BACK));

        draw::sceneBottom();
        drawHeader(lang::get(lang::STR_SETTINGS_PREFERENCES));

        float optY = theme::headerH + 10;
        float optH = 46;
        float optW = 300;
        float optX = 10;

        // Button 1: Language
        draw::panel(optX, optY, optW, optH, theme::surface, 8);
        draw::panelOutline(optX, optY, optW, optH, theme::accentDim, 1.0f, 8);
        draw::text(optX + 16, optY + 8, FS_SMALL, theme::textDim, lang::get(lang::STR_SETTINGS_LANG));
        draw::text(optX + 16, optY + 24, FS_BODY, theme::text, lang::name(config::current.langId));

        // Button 2: Theme
        float optY2 = optY + optH + 12;
        draw::panel(optX, optY2, optW, optH, theme::surface, 8);
        draw::panelOutline(optX, optY2, optW, optH, theme::accentDim, 1.0f, 8);
        draw::text(optX + 16, optY2 + 8, FS_SMALL, theme::textDim, lang::get(lang::STR_SETTINGS_THEME));
        draw::text(optX + 16, optY2 + 24, FS_BODY, theme::text, lang::themeName(config::current.themeId));

        drawHint(true, lang::get(lang::STR_HINT_TAP_BACK));
        draw::frameEnd();

        if (k & KEY_TOUCH)
        {
            if (touch.px >= optX && touch.px <= optX + optW)
            {
                if (touch.py >= optY && touch.py <= optY + optH)
                {
                    int c = menu(lang::get(lang::STR_SETTINGS_LANG),
                                 {lang::get(lang::STR_LANG_AUTO),
                                  lang::get(lang::STR_LANG_EN),
                                  lang::get(lang::STR_LANG_ES),
                                  lang::get(lang::STR_LANG_FR),
                                  lang::get(lang::STR_LANG_DE),
                                  lang::get(lang::STR_LANG_IT),
                                  lang::get(lang::STR_LANG_PT)});
                    if (c >= 0)
                    {
                        config::current.langId = static_cast<lang::Id>(c);
                        config::apply();
                        config::save();
                        toast(lang::get(lang::STR_TOAST_CONFIG_SAVED), theme::good);
                    }
                }
                else if (touch.py >= optY2 && touch.py <= optY2 + optH)
                {
                    int c = menu(lang::get(lang::STR_SETTINGS_THEME),
                                 {lang::get(lang::STR_THEME_DARK),
                                  lang::get(lang::STR_THEME_LIGHT),
                                  lang::get(lang::STR_THEME_MIDNIGHT)});
                    if (c >= 0)
                    {
                        config::current.themeId = static_cast<theme::Type>(c);
                        config::apply();
                        config::save();
                        toast(lang::get(lang::STR_TOAST_CONFIG_SAVED), theme::good);
                    }
                }
            }
        }
    }
}

void runHelp()
{
    while (aptMainLoop())
    {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & (KEY_B | KEY_SELECT | KEY_A | KEY_TOUCH)) return;

        draw::frameBegin();
        draw::sceneTop();
        drawHeader(lang::get(lang::STR_HELP_TITLE), APP_VERSION);
        float x = theme::pad, y = theme::headerH + 8;
        const char* lines[] = {
            lang::get(lang::STR_HELP_1),
            lang::get(lang::STR_HELP_2),
            lang::get(lang::STR_HELP_3),
            lang::get(lang::STR_HELP_4),
            lang::get(lang::STR_HELP_5),
            lang::get(lang::STR_HELP_6),
            lang::get(lang::STR_HELP_7),
            lang::get(lang::STR_HELP_8),
            lang::get(lang::STR_HELP_9),
            lang::get(lang::STR_HELP_10),
            lang::get(lang::STR_HELP_11),
        };
        for (auto l : lines) {
            u32 c = (l[0] == '!') ? theme::warn : theme::text;
            float sz = (l[0] == '!') ? FS_SMALL : FS_BODY;
            draw::text(x, y, sz, c, l);
            y += (l[0] == '!') ? 15 : 17;
        }
        drawHint(false, lang::get(lang::STR_HINT_CLOSE));
        draw::sceneBottom();
        draw::frameEnd();
    }
}

// -----------------------------------------------------------------------------
//  A-button / Touch: context menus
// -----------------------------------------------------------------------------
void openSlotRowMenu()
{
    const wifi::Slot& slot = S.console[S.selSlot];
    bool exists = S.consoleOk[S.selSlot] && slot.exists;
    char slotTitleBuf[64];
    std::snprintf(slotTitleBuf, sizeof(slotTitleBuf), lang::get(lang::STR_SLOT_TITLE), S.selSlot + 1);
    std::string slotTitle = slotTitleBuf;

    if (exists)
    {
        int c = menu(slotTitle,
                     {lang::get(lang::STR_MENU_BACKUP_TO_SD),
                      lang::get(lang::STR_MENU_VIEW_DETAILS),
                      lang::get(lang::STR_MENU_SHARE_QR),
                      lang::get(lang::STR_MENU_EDIT_NET),
                      lang::get(lang::STR_MENU_CLEAR_SLOT)});

        switch (c)
        {
            case 0:
                saveSlotAsBackup(slot);
                break;
            case 1:
                showSlotDetails(slotTitle, slot);
                break;
            case 2:
                runShareQr(wifi::credentialsFromSlot(slot), slotTitle);
                break;
            case 3:
            {
                wifi::Credentials cred = wifi::credentialsFromSlot(slot);
                if (runEditor(cred, "Edit " + slotTitle))
                {
                    wifi::Slot built = wifi::fromCredentials(cred, &slot);
                    if (R_SUCCEEDED(wifi::writeConsoleSlot(S.selSlot, built)))
                    {
                        refreshConsole();
                        char okBuf[64];
                        std::snprintf(okBuf, sizeof(okBuf), lang::get(lang::STR_TOAST_SLOT_WRITTEN), S.selSlot + 1);
                        toast(okBuf, theme::good);
                    }
                    else toast("Could not write slot", theme::danger);
                }
                break;
            }
            case 4:
            {
                char cTitle[64], cBody[128];
                std::snprintf(cTitle, sizeof(cTitle), lang::get(lang::STR_CONFIRM_CLEAR_TITLE), S.selSlot + 1);
                std::snprintf(cBody, sizeof(cBody), lang::get(lang::STR_CONFIRM_CLEAR_BODY), S.selSlot + 1);
                if (confirm(cTitle, cBody))
                {
                    wifi::Slot emptySlot{};
                    if (R_SUCCEEDED(wifi::writeConsoleSlot(S.selSlot, emptySlot)))
                    {
                        refreshConsole();
                        char clBuf[64];
                        std::snprintf(clBuf, sizeof(clBuf), lang::get(lang::STR_TOAST_SLOT_CLEARED), S.selSlot + 1);
                        toast(clBuf, theme::warn);
                    }
                    else toast("Clear failed", theme::danger);
                }
                break;
            }
            default: break;
        }
    }
    else
    {
        int c = menu(slotTitle + " (" + lang::get(lang::STR_SLOT_EMPTY) + ")",
                     {lang::get(lang::STR_MENU_CONFIG_NEW)});
        if (c == 0)
        {
            wifi::Credentials cred;
            if (runEditor(cred, "New " + slotTitle))
            {
                const wifi::Slot* templ = nullptr;
                for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
                    if (S.consoleOk[i] && S.console[i].exists) { templ = &S.console[i]; break; }
                wifi::Slot built = wifi::fromCredentials(cred, templ);
                if (R_SUCCEEDED(wifi::writeConsoleSlot(S.selSlot, built)))
                {
                    refreshConsole();
                    char okBuf[64];
                    std::snprintf(okBuf, sizeof(okBuf), lang::get(lang::STR_TOAST_SLOT_WRITTEN), S.selSlot + 1);
                    toast(okBuf, theme::good);
                }
                else toast("Could not write slot", theme::danger);
            }
        }
    }
}

void openBackupRowMenu()
{
    wifi::Backup* p = selBackup();
    if (!p) return;
    wifi::Backup bk = *p;

    char restBuf[64];
    std::snprintf(restBuf, sizeof(restBuf), lang::get(lang::STR_MENU_RESTORE_TO), S.selSlot + 1);

    int c = menu(bk.displayName,
                 {lang::get(lang::STR_MENU_VIEW_DETAILS),
                  restBuf,
                  lang::get(lang::STR_MENU_SHARE_QR),
                  lang::get(lang::STR_MENU_EDIT_NET),
                  lang::get(lang::STR_MENU_RENAME),
                  lang::get(lang::STR_MENU_DELETE)});

    switch (c)
    {
        case 0:
            showSlotDetails(bk.displayName, bk.slot);
            break;

        case 1:
        {
            char cTitle[64], cBody[160];
            std::snprintf(cTitle, sizeof(cTitle), lang::get(lang::STR_CONFIRM_APPLY_TITLE), S.selSlot + 1);
            std::snprintf(cBody, sizeof(cBody), lang::get(lang::STR_CONFIRM_APPLY_BODY), S.selSlot + 1, bk.displayName.c_str());
            if (confirm(cTitle, cBody))
            {
                if (R_SUCCEEDED(wifi::writeConsoleSlot(S.selSlot, bk.slot)))
                {
                    refreshConsole();
                    char okBuf[64];
                    std::snprintf(okBuf, sizeof(okBuf), lang::get(lang::STR_TOAST_SLOT_WRITTEN), S.selSlot + 1);
                    toast(okBuf, theme::good);
                }
                else toast("Write failed", theme::danger);
            }
            break;
        }

        case 2:
            runShareQr(wifi::credentialsFromSlot(bk.slot), bk.displayName);
            break;

        case 3:
        {
            wifi::Credentials cred = wifi::credentialsFromSlot(bk.slot);
            if (runEditor(cred, std::string(lang::get(lang::STR_MENU_EDIT_NET)) + " \"" + bk.displayName + "\""))
            {
                wifi::Slot built = wifi::fromCredentials(cred, &bk.slot);
                if (R_SUCCEEDED(wifi::overwriteBackup(bk.path, built, bk.alias, bk.savedEpoch)))
                {
                    refreshBackups();
                    clampSelRow();
                    toast(lang::get(lang::STR_TOAST_BACKUP_UPDATED), theme::good);
                }
                else toast("Could not save", theme::danger);
            }
            break;
        }

        case 4:
        {
            std::string alias;
            if (kbd::text(alias, lang::get(lang::STR_PROMPT_RENAME), bk.alias, 96, false, true))
            {
                if (R_SUCCEEDED(wifi::overwriteBackup(bk.path, bk.slot, alias, bk.savedEpoch)))
                {
                    refreshBackups();
                    clampSelRow();
                    toast(lang::get(lang::STR_TOAST_RENAMED), theme::good);
                }
                else toast("Could not save", theme::danger);
            }
            break;
        }

        case 5:
        {
            char cTitle[64], cBody[160];
            std::snprintf(cTitle, sizeof(cTitle), "%s", lang::get(lang::STR_CONFIRM_DELETE_TITLE));
            std::snprintf(cBody, sizeof(cBody), lang::get(lang::STR_CONFIRM_DELETE_BODY), bk.displayName.c_str());
            if (confirm(cTitle, cBody))
            {
                wifi::deleteBackupFile(bk.path);
                refreshBackups();
                clampSelRow();
                toast(lang::get(lang::STR_TOAST_BACKUP_DELETED), theme::warn);
            }
            break;
        }

        default: break;
    }
}

void homeInput(u32 kDown, u32 kHeld, u32 kUp)
{
    // R button: backup selected slot
    if (kDown & KEY_R)
    {
        const wifi::Slot& slot = S.console[S.selSlot];
        saveSlotAsBackup(slot);
        return;
    }

    if (kDown & KEY_L) { runScan(); return; }
    if (kDown & KEY_SELECT) { runHelp(); return; }

    if (kDown & KEY_DRIGHT) S.selSlot = (S.selSlot + 1) % CFG_WIFI_SLOTS;
    if (kDown & KEY_DLEFT)  S.selSlot = (S.selSlot + CFG_WIFI_SLOTS - 1) % CFG_WIFI_SLOTS;

    if (kDown & KEY_DDOWN)
    {
        if (!S.backups.empty())
        {
            S.selRow = (S.selRow + 1) % rowCount();
            clampSelRow();
        }
    }
    if (kDown & KEY_DUP)
    {
        if (!S.backups.empty())
        {
            S.selRow = (S.selRow + rowCount() - 1) % rowCount();
            clampSelRow();
        }
    }

    if (kDown & KEY_Y) S.showPw = !S.showPw;

    if (kDown & KEY_X)
    {
        wifi::Backup* b = selBackup();
        if (b)
        {
            char cTitle[64], cBody[160];
            std::snprintf(cTitle, sizeof(cTitle), "%s", lang::get(lang::STR_CONFIRM_DELETE_TITLE));
            std::snprintf(cBody, sizeof(cBody), lang::get(lang::STR_CONFIRM_DELETE_BODY), b->displayName.c_str());
            if (confirm(cTitle, cBody))
            {
                wifi::deleteBackupFile(b->path);
                refreshBackups();
                clampSelRow();
                toast(lang::get(lang::STR_TOAST_BACKUP_DELETED), theme::warn);
            }
        }
    }

    if (kDown & KEY_A)
    {
        if (!S.backups.empty()) openBackupRowMenu();
        else openSlotRowMenu();
    }

    // Touch Screen Processing
    touchPosition touch;
    hidTouchRead(&touch);

    float rowY = 8, rowH = 38;
    float slotW = 62, slotGap = 6;
    float listY = rowY + rowH + 8;
    float listH = 240 - listY - theme::hintH - 6;
    float listW = 320 - theme::pad * 2;
    float sbW = 5.0f;
    float sbX = theme::pad + listW - sbW - 3.0f;
    float sbY = listY + 4.0f;
    float sbH = listH - 8.0f;

    int totalBackups = static_cast<int>(S.backups.size());
    int maxScroll = std::max(0, totalBackups - kListVisible);

    if (kDown & KEY_TOUCH)
    {
        g_touch.isDown = true;
        g_touch.startPos = touch;
        g_touch.lastPos = touch;
        g_touch.startScroll = S.scroll;
        g_touch.isDraggingList = false;
        g_touch.isDraggingScrollbar = false;

        // Check if touched scrollbar area
        if (touch.px >= sbX - 8 && touch.px <= 320 &&
            touch.py >= listY && touch.py <= listY + listH)
        {
            g_touch.isDraggingScrollbar = true;
            if (maxScroll > 0)
            {
                float relY = (static_cast<float>(touch.py) - sbY) / sbH;
                if (relY < 0.0f) relY = 0.0f;
                if (relY > 1.0f) relY = 1.0f;
                S.scroll = static_cast<int>(relY * static_cast<float>(maxScroll) + 0.5f);
                if (S.scroll > maxScroll) S.scroll = maxScroll;
            }
        }
    }
    else if (kHeld & KEY_TOUCH)
    {
        if (g_touch.isDraggingScrollbar)
        {
            if (maxScroll > 0)
            {
                float relY = (static_cast<float>(touch.py) - sbY) / sbH;
                if (relY < 0.0f) relY = 0.0f;
                if (relY > 1.0f) relY = 1.0f;
                S.scroll = static_cast<int>(relY * static_cast<float>(maxScroll) + 0.5f);
                if (S.scroll > maxScroll) S.scroll = maxScroll;
                if (S.scroll < 0) S.scroll = 0;
            }
        }
        else
        {
            int dy = touch.py - g_touch.startPos.py;
            if (std::abs(dy) > 7)
            {
                g_touch.isDraggingList = true;
            }
            if (g_touch.isDraggingList && maxScroll > 0)
            {
                int deltaItems = -dy / 26;
                S.scroll = g_touch.startScroll + deltaItems;
                if (S.scroll > maxScroll) S.scroll = maxScroll;
                if (S.scroll < 0) S.scroll = 0;
            }
        }
        g_touch.lastPos = touch;
    }
    else if (kUp & KEY_TOUCH)
    {
        if (!g_touch.isDraggingList && !g_touch.isDraggingScrollbar)
        {
            int px = g_touch.startPos.px;
            int py = g_touch.startPos.py;

            // 1. Tapped in top row (Slots / QR / Settings)
            if (py >= rowY && py <= rowY + rowH)
            {
                for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
                {
                    float sx = theme::pad + i * (slotW + slotGap);
                    if (px >= sx && px <= sx + slotW)
                    {
                        if (S.selSlot == i)
                        {
                            openSlotRowMenu();
                        }
                        else
                        {
                            S.selSlot = i;
                        }
                        break;
                    }
                }

                // QR Scan button
                float qrX = theme::pad + 3 * (slotW + slotGap) + 4;
                float qrW = 38;
                if (px >= qrX && px <= qrX + qrW)
                {
                    runScan();
                }

                // Settings button
                float setX = qrX + qrW + 6;
                float setW = 38;
                if (px >= setX && px <= setX + setW)
                {
                    runSettings();
                }
            }
            // 2. Tapped on Backups List
            else if (py >= listY && py <= listY + listH && px >= theme::pad && px < sbX - 4)
            {
                if (totalBackups > 0)
                {
                    float itemH = listH / static_cast<float>(kListVisible);
                    int clickedIdx = static_cast<int>((py - listY) / itemH);
                    int r = S.scroll + clickedIdx;
                    if (r >= 0 && r < totalBackups)
                    {
                        if (S.selRow == r)
                        {
                            openBackupRowMenu();
                        }
                        else
                        {
                            S.selRow = r;
                        }
                    }
                }
            }
        }
        g_touch.isDown = false;
        g_touch.isDraggingList = false;
        g_touch.isDraggingScrollbar = false;
    }
}

} // namespace

// -----------------------------------------------------------------------------

namespace app
{

void run()
{
    lang::init();
    config::load();

    refreshConsole();
    refreshBackups();

    for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
        if (S.consoleOk[i] && S.console[i].exists) { S.selSlot = i; break; }

    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        u32 kUp   = hidKeysUp();
        if (kDown & KEY_START) break;

        homeInput(kDown, kHeld, kUp);

        draw::frameBegin();
        drawHome();
        draw::frameEnd();
    }
}

} // namespace app

