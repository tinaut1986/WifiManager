#include "app.h"

#include <3ds.h>
#include <vector>
#include <string>

#include "draw.h"
#include "theme.h"
#include "wifi.h"
#include "wifiqr.h"
#include "qrscan.h"
#include "qrgen.h"
#include "kbd.h"

extern "C" {
#include "file.h"
}

extern FS_Archive sdmcArchive;

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
    int  selRow  = 0;   // 0 = Scan QR, 1 = Backup current slot, >=2 = backups[selRow-2]
    int  scroll  = 0;
    bool showPw  = false;

    std::string toast;
    int         toastTimer = 0;
    u32         toastColor  = theme::accent;
} S;

const int kListVisible = 6;

int  rowCount()         { return 2 + static_cast<int>(S.backups.size()); }
bool rowIsBackup(int r) { return r >= 2; }
wifi::Backup* selBackup()
{
    return rowIsBackup(S.selRow) ? &S.backups[S.selRow - 2] : nullptr;
}

void clampSelRow()
{
    if (S.selRow >= rowCount()) S.selRow = rowCount() - 1;
    if (S.selRow < 0) S.selRow = 0;
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
        case wifi::Security::Open: return "Open";
        case wifi::Security::WEP:  return "WEP";
        case wifi::Security::WPA:  return "WPA";
        default:                   return "WPA2";
    }
}

// -----------------------------------------------------------------------------
//  Shared chrome
// -----------------------------------------------------------------------------
void drawHeader(const std::string& title, const std::string& right = "")
{
    draw::rect(0, 0, 400, theme::headerH, theme::surface);
    draw::rect(0, theme::headerH, 400, 2, theme::accent);
    draw::text(theme::pad, 7, FS_HEADER, theme::text, title);
    if (!right.empty())
        draw::text(400 - theme::pad, 8, FS_SMALL, theme::textDim, right, AlignRight);
}

void drawHint(bool bottom, const std::string& hint)
{
    float w = bottom ? 320 : 400;
    float y = 240 - theme::hintH;
    draw::rect(0, y, w, theme::hintH, theme::surface);
    draw::text(theme::pad, y + 4, FS_HINT, theme::textDim, hint);
}

void drawToast()
{
    if (S.toastTimer <= 0) return;
    S.toastTimer--;
    float tw = draw::textWidth(S.toast, FS_BODY) + 24;
    float x = (400 - tw) / 2;
    draw::panel(x, 198, tw, 24, theme::surfaceAlt, 12);
    draw::rect(x, 198, 3, 24, S.toastColor);
    draw::text(x + 14, 204, FS_BODY, theme::text, S.toast);
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
        if (k & KEY_A) return true;
        if (k & KEY_B) return false;

        draw::frameBegin();
        draw::sceneTop();
        draw::rect(0, 0, 400, 240, theme::overlay);
        float bw = 320, bh = 130, bx = 40, by = 55;
        draw::panel(bx, by, bw, bh, theme::surface, theme::radius);
        draw::rect(bx, by, bw, 3, theme::accent);
        draw::text(bx + bw / 2, by + 12, FS_TITLE, theme::text, title, AlignCenter);
        draw::textWrapped(bx + 16, by + 38, FS_BODY, theme::textDim, body, bw - 32);
        draw::text(bx + bw / 2, by + bh - 22, FS_BODY, theme::text,
                   "A: confirm      B: cancel", AlignCenter);
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
        int n = (int)opts.size();
        if (k & KEY_B) return -1;
        if (k & KEY_DDOWN) sel = (sel + 1) % n;
        if (k & KEY_DUP)   sel = (sel + n - 1) % n;
        if (k & KEY_A) return sel;

        float rowH = 26;
        float bh = 46 + rowH * opts.size();
        float bw = 300, bx = 50, by = (240 - bh) / 2;

        draw::frameBegin();
        draw::sceneTop();
        draw::rect(0, 0, 400, 240, theme::overlay);
        draw::panel(bx, by, bw, bh, theme::surface, theme::radius);
        draw::rect(bx, by, bw, 3, theme::accent);
        draw::text(bx + 16, by + 12, FS_TITLE, theme::text, title);
        for (size_t i = 0; i < opts.size(); ++i)
        {
            float ry = by + 36 + i * rowH;
            if ((int)i == sel)
                draw::panel(bx + 8, ry - 2, bw - 16, rowH - 2, theme::accentDim, 6);
            draw::text(bx + 20, ry + 3, FS_BODY, theme::text, opts[i]);
        }
        draw::text(bx + bw / 2, by + bh - 18, FS_SMALL, theme::textDim,
                   "A: choose    B: cancel", AlignCenter);
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
            if (field == 0 && kbd::text(v, "Network name (SSID)", cred.ssid, 32))
                cred.ssid = v;
            else if (field == 2 && kbd::text(v, "Password", cred.password, 63, false, true))
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
            draw::text(x + 10, y + 17, FS_BODY, theme::text, val);
            y += rh + 6;
        };
        row(0, "SSID  (press A to edit)", cred.ssid.empty() ? "(empty)" : cred.ssid);
        row(1, "Security  (D-pad to change)", secName(cred.security));
        row(2, "Password  (press A to edit)",
            cred.security == wifi::Security::Open ? "(not needed)"
            : cred.password.empty() ? "(none)" : cred.password);

        drawHint(false, "D-pad: move    A: edit    X: done    B: cancel");
        draw::sceneBottom();
        drawHint(true, "Edit the fields, then press X to continue.");
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
        drawHeader("Share network", subtitle);

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
                       "Cannot build a QR for this network", AlignCenter);
        }

        draw::sceneBottom();
        drawHeader("");
        float y = theme::headerH + 12;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, "SSID");
        draw::text(theme::pad, y + 13, FS_BODY, theme::text,
                   cred.ssid.empty() ? "(none)" : cred.ssid);
        y += 40;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, "Security");
        draw::text(theme::pad, y + 13, FS_BODY, theme::text, secName(cred.security));
        y += 40;
        draw::text(theme::pad, y, FS_SMALL, theme::textDim, "Payload");
        draw::textWrapped(theme::pad, y + 13, FS_SMALL, theme::textDim, uri, 320 - theme::pad * 2);

        drawHint(true, "B: close");
        draw::frameEnd();
    }
}

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

    draw::text(x + 12, y + 9, FS_BODY, theme::textDim, heading);
    float ty = y + 26;
    if (!sub.empty()) { draw::text(x + 12, ty, FS_SMALL, theme::textDim, sub); ty += 15; }

    if (!exists || !slot)
    {
        draw::text(x + 12, y + h / 2, FS_BODY, theme::textDim, "- empty -");
        return;
    }

    ty += 6;
    draw::text(x + 12, ty, FS_TITLE, theme::text,
               slot->ssid.empty() ? "(no SSID)" : slot->ssid);
    ty += 22;
    drawBadge(x + 12, ty, wifi::securityLabel(slot->encryption),
              securityColor(slot->encryption));
    ty += 22;

    std::string pw = slot->encryption == WIFI_ENCRYPTION_OPEN
                         ? "(no password)"
                         : (S.showPw ? slot->password : std::string(slot->password.size(), '*'));
    draw::text(x + 12, ty, FS_SMALL, theme::textDim, "Password");
    draw::textWrapped(x + 12, ty + 14, FS_BODY, theme::text, pw, w - 24);
}

void drawHome()
{
    draw::sceneTop();
    drawHeader("WifiManager", "SELECT / R: help");

    float cardY = theme::headerH + 10;
    float cardH = 240 - cardY - theme::hintH - 10;
    drawSlotCard(theme::pad, cardY, 185, cardH,
                 "Console slot " + std::to_string(S.selSlot + 1), "",
                 S.consoleOk[S.selSlot] && S.console[S.selSlot].exists,
                 &S.console[S.selSlot], true);

    wifi::Backup* b = selBackup();
    drawSlotCard(205, cardY, 185, cardH,
                 b ? b->displayName : "Backup",
                 b ? (b->savedDate.empty() ? std::string("saved: unknown date")
                                           : "saved " + b->savedDate)
                   : "",
                 b != nullptr, b ? &b->slot : nullptr, false);

    drawHint(false, "L: scan QR    SELECT/R: help    START: quit");
    drawToast();

    draw::sceneBottom();
    float rowY = 8, rowH = 38, gap = 8;
    float sw = (320 - theme::pad * 2 - gap * 2) / 3;
    for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
    {
        float sx = theme::pad + i * (sw + gap);
        if (i == S.selSlot) draw::panel(sx - 2, rowY - 2, sw + 4, rowH + 4, theme::accent, 8);
        draw::panel(sx, rowY, sw, rowH, theme::surface, 8);
        draw::text(sx + sw / 2, rowY + 4, FS_BODY, theme::text,
                   "Slot " + std::to_string(i + 1), AlignCenter);
        bool ok = S.consoleOk[i] && S.console[i].exists;
        draw::text(sx + sw / 2, rowY + 20, FS_SMALL,
                   ok ? theme::good : theme::textDim,
                   ok ? "configured" : "empty", AlignCenter);
    }

    float listY = rowY + rowH + 10;
    float listH = 240 - listY - theme::hintH - 6;
    draw::panel(theme::pad, listY, 320 - theme::pad * 2, listH, theme::surface, 8);

    if (S.selRow < S.scroll) S.scroll = S.selRow;
    if (S.selRow >= S.scroll + kListVisible) S.scroll = S.selRow - kListVisible + 1;

    float itemH = listH / kListVisible;
    for (int v = 0; v < kListVisible; ++v)
    {
        int r = S.scroll + v;
        if (r >= rowCount()) break;
        float iy = listY + v * itemH;
        if (r == S.selRow)
            draw::panel(theme::pad + 3, iy + 2, 320 - theme::pad * 2 - 6, itemH - 4,
                        theme::accentDim, 6);

        std::string label, rightText;
        u32 col = theme::text;
        if (r == 0)      { label = "Scan Wi-Fi QR code"; col = theme::accent; }
        else if (r == 1) { label = "New backup from Slot " + std::to_string(S.selSlot + 1); col = theme::accent; }
        else
        {
            const wifi::Backup& bk = S.backups[r - 2];
            label = bk.displayName;
            rightText = bk.savedDate;
        }

        draw::text(theme::pad + 14, iy + itemH / 2 - 7, FS_BODY, col, label);
        if (!rightText.empty())
            draw::text(320 - theme::pad - 8, iy + itemH / 2 - 6, FS_SMALL,
                       theme::textDim, rightText, AlignRight);
    }

    drawHint(true, "A: options   X: delete   Y: show/hide password");
}

// -----------------------------------------------------------------------------
//  Flows
// -----------------------------------------------------------------------------
void saveSlotAsBackup(const wifi::Slot& slot)
{
    std::string alias;
    if (!kbd::text(alias, "Optional alias  (empty = use the network name)",
                   "", 96, false, true))
        return;

    std::string path = wifi::saveBackup(slot, alias);
    if (!path.empty())
    {
        refreshBackups();
        clampSelRow();
        toast(alias.empty() ? "Backup saved" : "Backup saved: " + alias, theme::good);
    }
    else toast("Could not write backup", theme::danger);
}

void importCredentials(wifi::Credentials cred, bool fromUri)
{
    if (!runEditor(cred, fromUri ? "Review (from WIFI: QR)" : "Review (from text QR)"))
        return;

    int choice = menu("Save where?",
                      {"Save as backup", "Write to Slot 1", "Write to Slot 2", "Write to Slot 3"});
    if (choice < 0) return;

    const wifi::Slot* templ = nullptr;
    for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
        if (S.consoleOk[i] && S.console[i].exists) { templ = &S.console[i]; break; }
    wifi::Slot built = wifi::fromCredentials(cred, templ);

    if (choice == 0) { saveSlotAsBackup(built); return; }

    int slot = choice - 1;
    if (confirm("Write to Slot " + std::to_string(slot + 1) + "?",
                "Replace the network in console Slot " + std::to_string(slot + 1) +
                " with \"" + cred.ssid + "\"?"))
    {
        if (R_SUCCEEDED(wifi::writeConsoleSlot(slot, built)))
        {
            refreshConsole();
            toast("Written to Slot " + std::to_string(slot + 1), theme::good);
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
        if (hidKeysDown() & KEY_B) break;
        if (qrscan::failed()) { toast("Camera error", theme::danger); break; }
        if (qrscan::update(payload)) got = true;

        draw::frameBegin();
        draw::sceneTop();
        if (qrscan::hasPreview())
            C2D_DrawImageAt(qrscan::preview(), 0, 0, 0.0f, nullptr, 1.0f, 1.0f);
        else
            draw::text(200, 112, FS_BODY, theme::text, "Starting camera...", AlignCenter);

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
        drawHeader("Scan QR");
        draw::text(160, 44, FS_BODY, theme::text, "Point at a Wi-Fi QR code", AlignCenter);
        draw::text(160, 68, FS_SMALL, theme::textDim, "Standard \"WIFI:\" codes and", AlignCenter);
        draw::text(160, 82, FS_SMALL, theme::textDim, "plain SSID / password text.", AlignCenter);
        drawHint(true, "B: cancel");
        draw::frameEnd();

        if (got) break;
    }

    qrscan::exit();
    if (!got) return;

    wifiqr::ParseResult pr = wifiqr::parse(payload);
    if (!pr.ok) { toast(pr.error.empty() ? "Unrecognised QR code" : pr.error, theme::danger); return; }
    importCredentials(pr.cred, pr.fromStandardUri);
}

void runHelp()
{
    while (aptMainLoop())
    {
        hidScanInput();
        if (hidKeysDown() & (KEY_B | KEY_SELECT | KEY_R | KEY_A)) return;

        draw::frameBegin();
        draw::sceneTop();
        drawHeader("How to use");
        float x = theme::pad, y = theme::headerH + 12;
        const char* lines[] = {
            "D-pad Left/Right   switch console slot",
            "D-pad Up/Down      move through the list",
            "A                  open options for the selected entry",
            "                   (restore / share QR / edit / rename)",
            "X                  delete the selected backup",
            "Y                  show / hide passwords",
            "L                  open the QR scanner",
            "SELECT or R        this help",
            "START              quit",
        };
        for (auto l : lines) { draw::text(x, y, FS_BODY, theme::text, l); y += 20; }
        drawHint(false, "B / SELECT / R: close");
        draw::sceneBottom();
        draw::frameEnd();
    }
}

// -----------------------------------------------------------------------------
//  A-button: context menus
// -----------------------------------------------------------------------------
void openSlotRowMenu()
{
    const wifi::Slot& slot = S.console[S.selSlot];
    if (!(S.consoleOk[S.selSlot] && slot.exists))
    {
        toast("Slot " + std::to_string(S.selSlot + 1) + " is empty", theme::warn);
        return;
    }
    int c = menu("Slot " + std::to_string(S.selSlot + 1),
                 {"Back up to SD card", "Share as QR code"});
    if (c == 0) saveSlotAsBackup(slot);
    else if (c == 1) runShareQr(wifi::credentialsFromSlot(slot),
                                "Console Slot " + std::to_string(S.selSlot + 1));
}

void openBackupRowMenu()
{
    wifi::Backup* p = selBackup();
    if (!p) return;
    wifi::Backup bk = *p;   // copy: the list may be rebuilt below

    int c = menu(bk.displayName,
                 {"Restore to Slot " + std::to_string(S.selSlot + 1),
                  "Share as QR code", "Edit network", "Rename alias", "Delete"});

    switch (c)
    {
        case 0:
            if (confirm("Write to Slot " + std::to_string(S.selSlot + 1) + "?",
                        "Replace the network in console Slot " +
                        std::to_string(S.selSlot + 1) + " with \"" + bk.displayName + "\"?"))
            {
                if (R_SUCCEEDED(wifi::writeConsoleSlot(S.selSlot, bk.slot)))
                {
                    refreshConsole();
                    toast("Applied to Slot " + std::to_string(S.selSlot + 1), theme::good);
                }
                else toast("Write failed", theme::danger);
            }
            break;

        case 1:
            runShareQr(wifi::credentialsFromSlot(bk.slot), bk.displayName);
            break;

        case 2:
        {
            wifi::Credentials cred = wifi::credentialsFromSlot(bk.slot);
            if (runEditor(cred, "Edit \"" + bk.displayName + "\""))
            {
                wifi::Slot built = wifi::fromCredentials(cred, &bk.slot);
                if (R_SUCCEEDED(wifi::overwriteBackup(bk.path, built, bk.alias, bk.savedEpoch)))
                {
                    refreshBackups();
                    clampSelRow();
                    toast("Backup updated", theme::good);
                }
                else toast("Could not save", theme::danger);
            }
            break;
        }

        case 3:
        {
            std::string alias;
            if (kbd::text(alias, "Alias  (empty = use the network name)", bk.alias, 96, false, true))
            {
                if (R_SUCCEEDED(wifi::overwriteBackup(bk.path, bk.slot, alias, bk.savedEpoch)))
                {
                    refreshBackups();
                    clampSelRow();
                    toast("Renamed", theme::good);
                }
                else toast("Could not save", theme::danger);
            }
            break;
        }

        case 4:
            if (confirm("Delete backup?",
                        "Permanently delete \"" + bk.displayName + "\" from the SD card?"))
            {
                wifi::deleteBackupFile(bk.path);
                refreshBackups();
                clampSelRow();
                toast("Backup deleted", theme::warn);
            }
            break;

        default: break;
    }
}

void homeInput(u32 k)
{
    if (k & KEY_L) { runScan(); return; }
    if (k & (KEY_SELECT | KEY_R)) { runHelp(); return; }

    if (k & KEY_DRIGHT) S.selSlot = (S.selSlot + 1) % CFG_WIFI_SLOTS;
    if (k & KEY_DLEFT)  S.selSlot = (S.selSlot + CFG_WIFI_SLOTS - 1) % CFG_WIFI_SLOTS;

    if (k & KEY_DDOWN) S.selRow = (S.selRow + 1) % rowCount();
    if (k & KEY_DUP)   S.selRow = (S.selRow + rowCount() - 1) % rowCount();

    if (k & KEY_Y) S.showPw = !S.showPw;

    if (k & KEY_X)
    {
        wifi::Backup* b = selBackup();
        if (b && confirm("Delete backup?",
                         "Permanently delete \"" + b->displayName + "\" from the SD card?"))
        {
            wifi::deleteBackupFile(b->path);
            refreshBackups();
            clampSelRow();
            toast("Backup deleted", theme::warn);
        }
    }

    if (k & KEY_A)
    {
        if (S.selRow == 0)      runScan();
        else if (S.selRow == 1) openSlotRowMenu();
        else                    openBackupRowMenu();
    }
}

} // namespace

// -----------------------------------------------------------------------------

namespace app
{

void run()
{
    refreshConsole();
    refreshBackups();

    for (int i = 0; i < CFG_WIFI_SLOTS; ++i)
        if (S.consoleOk[i] && S.console[i].exists) { S.selSlot = i; break; }

    while (aptMainLoop())
    {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_START) break;

        homeInput(k);

        draw::frameBegin();
        drawHome();
        draw::frameEnd();
    }
}

} // namespace app
