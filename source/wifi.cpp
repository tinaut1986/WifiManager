#include "wifi.h"

#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <sstream>
#include <iomanip>

extern "C" {
#include "file.h"
#include "checksum.h"
}

extern FS_Archive sdmcArchive;

namespace
{
    // Kept stable across the rename so existing backups stay in place.
    const char* kWorkingDir = "/3ds/WifiManager";

    std::string trimNul(const char* p, size_t maxLen)
    {
        size_t n = 0;
        while (n < maxLen && p[n] != '\0') ++n;
        return std::string(p, n);
    }

    std::string sanitizeFileBase(std::string s)
    {
        for (char& c : s)
            if (std::strchr("/\\:*?\"<>|", c) || static_cast<unsigned char>(c) < 0x20)
                c = '_';
        while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        if (s.empty()) s = "wifi";
        if (s.size() > 100) s.resize(100);
        return s;
    }

    bool sdmcFileExists(const std::string& path)
    {
        Handle h;
        if (R_FAILED(FSUSER_OpenFile(&h, sdmcArchive,
                fsMakePath(PATH_ASCII, path.c_str()), FS_OPEN_READ, 0)))
            return false;
        FSFILE_Close(h);
        return true;
    }

    // Trailer written after the raw slot struct:
    //   "\nWMB1\t<unix-epoch>\t<alias>\n"
    // Older files may instead carry "\nWMA1\t<alias>\n" (no timestamp).
    constexpr const char* kTagV2 = "WMB1\t";
    constexpr const char* kTagV1 = "WMA1\t";

    void parseTrailer(const std::string& tail, std::string* aliasOut, std::time_t* epochOut)
    {
        size_t p = tail.find(kTagV2);
        if (p != std::string::npos)
        {
            size_t s = p + std::strlen(kTagV2);
            size_t t = tail.find('\t', s);
            size_t e = tail.find('\n', s);
            if (t != std::string::npos && (e == std::string::npos || t < e))
            {
                if (epochOut) *epochOut = static_cast<std::time_t>(strtoll(tail.substr(s, t - s).c_str(), nullptr, 10));
                if (aliasOut) *aliasOut = tail.substr(t + 1, e == std::string::npos ? std::string::npos : e - t - 1);
                return;
            }
        }
        p = tail.find(kTagV1);
        if (p != std::string::npos)
        {
            size_t s = p + std::strlen(kTagV1);
            size_t e = tail.find('\n', s);
            if (aliasOut) *aliasOut = tail.substr(s, e == std::string::npos ? std::string::npos : e - s);
        }
    }

    std::string writeTrailer(std::time_t epoch, const std::string& alias)
    {
        return "\n" + std::string(kTagV2) + std::to_string((long long)epoch) + "\t" + alias + "\n";
    }
}

namespace wifi
{

const char* workingDir() { return kWorkingDir; }

void fixCrc(wifi_slot_s& s)
{
    s.crc_checksum = crc_16(reinterpret_cast<const unsigned char*>(&s) + 4, 0x410);
}

std::string securityLabel(int e)
{
    switch (e)
    {
        case WIFI_ENCRYPTION_OPEN:          return "Open";
        case WIFI_ENCRYPTION_WEP_64:        return "WEP 64";
        case WIFI_ENCRYPTION_WEP_128:       return "WEP 128";
        case WIFI_ENCRYPTION_WEP_152:       return "WEP 152";
        case WIFI_ENCRYPTION_WPA_PSK_TKIP:  return "WPA TKIP";
        case WIFI_ENCRYPTION_WPA_PSK_AES:   return "WPA AES";
        case WIFI_ENCRYPTION_WPA2_PSK_TKIP: return "WPA2 TKIP";
        case WIFI_ENCRYPTION_WPA2_PSK_AES:  return "WPA2 AES";
        default:                            return "Unknown";
    }
}

void decode(Slot& s)
{
    const wifi_slot_s& d = s.raw;
    s.exists           = d.version != 0;
    s.editableSecurity = d.editable_security != 0;
    s.wps              = d.multissid_type != 0;

    if (!s.exists) return;

    const sub_network_s& net = s.editableSecurity ? d.main_network : d.multissid_networks[0];

    size_t ssidLen = d.main_network.SSID_length;
    if (ssidLen > sizeof(net.SSID)) ssidLen = sizeof(net.SSID);
    s.ssid       = std::string(net.SSID, ssidLen);
    s.encryption = net.encryption_type;
    s.password   = trimNul(net.password_text, sizeof(net.password_text));

    bool wep = s.encryption == WIFI_ENCRYPTION_WEP_64 ||
               s.encryption == WIFI_ENCRYPTION_WEP_128 ||
               s.encryption == WIFI_ENCRYPTION_WEP_152;
    if (s.password.empty() && wep)
        s.password = trimNul(reinterpret_cast<const char*>(net.password_key), sizeof(net.password_key));
    if (s.password.empty() && s.encryption != WIFI_ENCRYPTION_OPEN)
        s.password = "(not available)";
}

// -----------------------------------------------------------------------------

bool readConsoleSlot(int index, Slot& out)
{
    out = Slot{};
    Result r = CFG_GetConfigInfoBlk8(sizeof(out.raw), CFG_WIFI_BLKID + index,
                                     reinterpret_cast<u8*>(&out.raw));
    if (R_FAILED(r)) return false;
    decode(out);
    return true;
}

// -----------------------------------------------------------------------------
//  Writing a slot so that the console uses it at once.
//
//  The system keeps the slots twice: in the config savegame (the blocks above)
//  and in the ac service, which reads them from the config once, at boot, and
//  connects with its own copy. Writing the blocks therefore changed nothing the
//  console used until System Settings was opened (and ac's next flush could
//  even write its old copy back). So a slot is written through ac:i, as System
//  Settings does: LoadNetworkSetting (InitializeNetworkSetting for an empty
//  slot), one setter per field, UpdateNetworkSetting, FlushNetworkSetting (ac
//  then saves the config itself). Command ids from 3dbrew's AC Services page.
//
//  The static buffer number each setter takes is not documented; found on a
//  New 3DS XL (11.17) by sending empty buffers (ac checks the number, not the
//  size): SSID 2, key 3, BSSID 4, passphrase 8, IP 9, gateway 10, netmask 11,
//  DNS 12, proxy host 13, proxy user 14, proxy password 15. A buffer LARGER than
//  what ac set aside for that number brings the whole console down (kernel
//  abort; seen with 0x40 bytes to number 2), so each field is sent with its own
//  size, never more. Same code as NSA's source/fbi/wifislots.c.
// -----------------------------------------------------------------------------

namespace
{
    Result aciCall(u32 header, u32 param)
    {
        u32* cmdbuf = getThreadCommandBuffer();
        cmdbuf[0] = header;
        cmdbuf[1] = param;
        Result r = svcSendSyncRequest(*acGetSessionHandle());
        return R_FAILED(r) ? r : static_cast<Result>(cmdbuf[1]);
    }

    Result aciSet(u16 command, u32 value)
    {
        return aciCall(IPC_MakeHeader(command, 1, 0), value);
    }

    Result aciSetStatic(u16 command, u32 id, const void* data, size_t size)
    {
        u32* cmdbuf = getThreadCommandBuffer();
        cmdbuf[0] = IPC_MakeHeader(command, 0, 2);
        cmdbuf[1] = IPC_Desc_StaticBuffer(size, id);
        cmdbuf[2] = reinterpret_cast<u32>(data);
        Result r = svcSendSyncRequest(*acGetSessionHandle());
        return R_FAILED(r) ? r : static_cast<Result>(cmdbuf[1]);
    }

    // The main network (setters 0x410-0x418) or WPS/AOSS setting n (0x420 + n * 10).
    Result aciSetNetwork(u16 base, const sub_network_s& n)
    {
        Result r = aciSetStatic(base, 2, n.SSID, sizeof(n.SSID));
        if (R_SUCCEEDED(r)) r = aciSet(base + 2, n.SSID_length);
        if (R_SUCCEEDED(r)) r = aciSet(base + 4, n.encryption_type);
        if (R_SUCCEEDED(r)) r = aciSetStatic(base + 6, 8, n.password_text, sizeof(n.password_text));
        if (R_SUCCEEDED(r)) r = aciSetStatic(base + 8, 3, n.password_key, sizeof(n.password_key));
        return r;
    }

    Result writeThroughAc(int index, const wifi_slot_s& d)
    {
        Result r;
#define WM_STEP(call) do { if (R_FAILED(r = (call))) return r; } while (0)
        // An empty slot: ac's own removal (the console forgets it at once too).
        if (d.version == 0)
        {
            WM_STEP(aciCall(IPC_MakeHeader(0x403, 1, 0), static_cast<u32>(index)));
            WM_STEP(aciCall(IPC_MakeHeader(0x404, 0, 0), 0));
            return 0;
        }

        if (R_FAILED(aciCall(IPC_MakeHeader(0x401, 1, 0), static_cast<u32>(index))))
            WM_STEP(aciCall(IPC_MakeHeader(0x406, 1, 0), 1));

        WM_STEP(aciSet(0x408, d.version));
        WM_STEP(aciSet(0x40C, d.network_enable));
        WM_STEP(aciSet(0x40E, d.editable_security));
        WM_STEP(aciSetNetwork(0x410, d.main_network));
        WM_STEP(aciSet(0x41A, d.multissid_enable));
        WM_STEP(aciSet(0x41C, d.multissid_type));
        WM_STEP(aciSet(0x41E, d.multissid_amount));
        for (u32 i = 0; i < 4; ++i)
            WM_STEP(aciSetNetwork(static_cast<u16>(0x420 + i * 10), d.multissid_networks[i]));
        WM_STEP(aciSet(0x448, d.enable_DHCP));
        WM_STEP(aciSet(0x44A, d.enable_autoDNS));
        WM_STEP(aciSetStatic(0x44C, 9, d.ip_address, sizeof(d.ip_address)));
        WM_STEP(aciSetStatic(0x44E, 10, d.gateway_address, sizeof(d.gateway_address)));
        WM_STEP(aciSetStatic(0x450, 11, d.subnet_mask, sizeof(d.subnet_mask)));
        WM_STEP(aciSetStatic(0x452, 12, d.dns_primary, sizeof(d.dns_primary)));
        WM_STEP(aciSetStatic(0x454, 12, d.dns_secondary, sizeof(d.dns_secondary)));
        WM_STEP(aciSet(0x456, d.connection_test_success));
        WM_STEP(aciSetStatic(0x45A, 4, d.AP_MAC_address, sizeof(d.AP_MAC_address)));
        WM_STEP(aciSet(0x460, d.enable_proxy));
        WM_STEP(aciSet(0x462, d.proxy_auth));
        WM_STEP(aciSet(0x464, d.port_number));
        WM_STEP(aciSetStatic(0x466, 13, d.proxy_url, sizeof(d.proxy_url)));
        WM_STEP(aciSetStatic(0x468, 14, d.proxy_username, sizeof(d.proxy_username)));
        WM_STEP(aciSetStatic(0x46A, 15, d.proxy_password, sizeof(d.proxy_password)));
        WM_STEP(aciSet(0x46C, d.UPnP_value));
        WM_STEP(aciSet(0x46E, d.MTU_value));
        WM_STEP(aciCall(IPC_MakeHeader(0x402, 1, 0), static_cast<u32>(index)));
        WM_STEP(aciCall(IPC_MakeHeader(0x404, 0, 0), 0));
#undef WM_STEP
        return 0;
    }
}

Result writeConsoleSlot(int index, const Slot& in)
{
    wifi_slot_s data = in.raw;
    fixCrc(data);

    // Through ac:i, so the console uses it at once (see above).
    Result r = writeThroughAc(index, data);
    if (R_SUCCEEDED(r)) return r;

    // No ac:i (an old firmware, or no Luma3DS): the config blocks, as before;
    // the console takes them up at its next boot (or after System Settings).
    r = CFG_SetConfigInfoBlk8(sizeof(data), CFG_WIFI_BLKID + index,
                              reinterpret_cast<u8*>(&data));
    if (R_SUCCEEDED(r)) r = CFG_UpdateConfigSavegame();
    return r;
}

// -----------------------------------------------------------------------------

std::string formatEpoch(std::time_t t)
{
    if (t == 0) return "";
    std::tm tm = *std::localtime(&t);
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

Credentials credentialsFromSlot(const Slot& s)
{
    Credentials c;
    c.ssid     = s.ssid;
    c.password = (s.password == "(not available)") ? "" : s.password;
    switch (s.encryption)
    {
        case WIFI_ENCRYPTION_OPEN:          c.security = Security::Open; break;
        case WIFI_ENCRYPTION_WEP_64:
        case WIFI_ENCRYPTION_WEP_128:
        case WIFI_ENCRYPTION_WEP_152:       c.security = Security::WEP;  break;
        case WIFI_ENCRYPTION_WPA_PSK_TKIP:
        case WIFI_ENCRYPTION_WPA_PSK_AES:   c.security = Security::WPA;  break;
        default:                            c.security = Security::WPA2; break;
    }
    return c;
}

bool readBackupFile(const std::string& path, Slot& out,
                    std::string* aliasOut, std::time_t* epochOut)
{
    out = Slot{};
    if (aliasOut) aliasOut->clear();
    if (epochOut) *epochOut = 0;

    Handle h;
    if (R_FAILED(openFile(path.c_str(), sdmcArchive, &h, false))) return false;

    u64 fsize = 0;
    FSFILE_GetSize(h, &fsize);

    u32 read = 0;
    FSFILE_Read(h, &read, 0, &out.raw, sizeof(out.raw));

    if (read >= sizeof(out.raw) && (aliasOut || epochOut) && fsize > sizeof(out.raw))
    {
        u32 tailLen = static_cast<u32>(std::min<u64>(fsize - sizeof(out.raw), 1024));
        std::string tail(tailLen, '\0');
        u32 tr = 0;
        FSFILE_Read(h, &tr, sizeof(out.raw), &tail[0], tailLen);
        tail.resize(tr);
        parseTrailer(tail, aliasOut, epochOut);
    }
    FSFILE_Close(h);

    if (read < sizeof(out.raw)) return false;
    decode(out);
    return true;
}

static Result writeBackupTo(const std::string& path, const Slot& in,
                            const std::string& alias, std::time_t epoch)
{
    wifi_slot_s data = in.raw;
    fixCrc(data);

    Handle h;
    Result r = openFile(path.c_str(), sdmcArchive, &h, true);
    if (R_FAILED(r)) return r;

    u32 wr = 0;
    r = FSFILE_Write(h, &wr, 0, &data, sizeof(data), 0);
    if (R_SUCCEEDED(r))
    {
        std::string meta = writeTrailer(epoch, alias);
        r = FSFILE_Write(h, &wr, sizeof(data), meta.data(), meta.size(), FS_WRITE_FLUSH);
    }
    FSFILE_Close(h);
    return r;
}

std::string saveBackup(const Slot& in, const std::string& alias)
{
    std::string dir = kWorkingDir;
    createFolderRecursively((dir + "/").c_str(), sdmcArchive);

    std::string base = sanitizeFileBase(!alias.empty() ? alias
                                       : (in.ssid.empty() ? "wifi" : in.ssid));
    std::string path = dir + "/" + base + ".bin";
    for (int i = 2; sdmcFileExists(path); ++i)
        path = dir + "/" + base + "_" + std::to_string(i) + ".bin";

    return R_SUCCEEDED(writeBackupTo(path, in, alias, std::time(nullptr))) ? path
                                                                          : std::string();
}

Result overwriteBackup(const std::string& path, const Slot& in,
                       const std::string& alias, std::time_t epoch)
{
    return writeBackupTo(path, in, alias, epoch ? epoch : std::time(nullptr));
}

Result deleteBackupFile(const std::string& path)
{
    return deleteFile(path.c_str(), sdmcArchive);
}

std::vector<Backup> listBackups()
{
    std::vector<Backup> out;
    Handle dir;
    if (R_FAILED(FSUSER_OpenDirectory(&dir, sdmcArchive,
            fsMakePath(PATH_ASCII, kWorkingDir))))
        return out;

    for (;;)
    {
        FS_DirectoryEntry e;
        u32 n = 0;
        if (R_FAILED(FSDIR_Read(dir, &n, 1, &e)) || n == 0) break;
        if (e.attributes & FS_ATTRIBUTE_DIRECTORY) continue;
        if (strcasecmp(e.shortExt, "BIN") != 0) continue;

        char name[0x200] = {0};
        utf16_to_utf8(reinterpret_cast<u8*>(name), e.name, sizeof(name) - 1);

        std::string fileBase = name;
        size_t dot = fileBase.find_last_of('.');
        if (dot != std::string::npos) fileBase.erase(dot);

        Backup b;
        b.path = std::string(kWorkingDir) + "/" + name;

        std::string alias;
        std::time_t epoch = 0;
        if (!readBackupFile(b.path, b.slot, &alias, &epoch)) continue;
        b.alias = alias;
        b.savedEpoch = epoch;
        b.savedDate = formatEpoch(epoch);
        b.displayName = !alias.empty() ? alias
                       : (b.slot.ssid.empty() ? fileBase : b.slot.ssid);

        out.push_back(std::move(b));
    }
    FSDIR_Close(dir);

    std::sort(out.begin(), out.end(), [](const Backup& a, const Backup& b) {
        if (a.savedEpoch != b.savedEpoch) return a.savedEpoch > b.savedEpoch; // newest first
        return a.displayName < b.displayName;
    });
    return out;
}

// -----------------------------------------------------------------------------

Slot fromCredentials(const Credentials& c, const Slot* templ)
{
    Slot s{};

    if (templ && templ->exists)
    {
        s.raw = templ->raw;
    }
    else
    {
        std::memset(&s.raw, 0, sizeof(s.raw));
        s.raw.version         = 1;
        s.raw.enable_DHCP     = 1;
        s.raw.enable_autoDNS  = 1;
        s.raw.MTU_value       = 1400;
    }

    // Force a "normal", editable-security network so the main_network fields
    // are the ones the console reads.
    s.raw.network_enable    = 1;
    s.raw.editable_security  = 1;
    s.raw.multissid_enable   = 0;
    s.raw.multissid_amount   = 0;
    s.raw.connection_test_success = 0;

    int enc;
    switch (c.security)
    {
        case Security::Open: enc = WIFI_ENCRYPTION_OPEN;         break;
        case Security::WEP:  enc = WIFI_ENCRYPTION_WEP_128;      break;
        case Security::WPA:  enc = WIFI_ENCRYPTION_WPA_PSK_AES;  break;
        case Security::WPA2:
        default:             enc = WIFI_ENCRYPTION_WPA2_PSK_AES; break;
    }

    sub_network_s& net = s.raw.main_network;
    std::memset(&net, 0, sizeof(net));

    std::string ssid = c.ssid.substr(0, sizeof(net.SSID));
    std::memcpy(net.SSID, ssid.data(), ssid.size());
    net.SSID_length     = static_cast<u8>(ssid.size());
    net.encryption_type = static_cast<u8>(enc);

    if (enc != WIFI_ENCRYPTION_OPEN)
    {
        std::string pw = c.password.substr(0, sizeof(net.password_text) - 1);
        std::memcpy(net.password_text, pw.data(), pw.size());
    }

    s.raw.main_network.SSID_length = net.SSID_length; // (same field, kept explicit)
    fixCrc(s.raw);
    decode(s);
    return s;
}

} // namespace wifi
