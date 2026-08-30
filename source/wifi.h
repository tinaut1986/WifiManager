#pragma once
#include <3ds.h>
#include <string>
#include <vector>
#include <ctime>

// -----------------------------------------------------------------------------
//  3DS WiFi configuration block model + persistence (console slots & backups).
//  The on-disk / in-config layout below is unchanged from the original project
//  (it is hardware/OS defined and must match exactly).
// -----------------------------------------------------------------------------

#define CFG_WIFI_BLKID     ((u32)0x00080000)
#define CFG_WIFI_SLOTS     3
#define CFG_WIFI_SLOT_SIZE ((u32)0xC00)

typedef enum {
    WIFI_ENCRYPTION_OPEN = 0,
    WIFI_ENCRYPTION_WEP_64,
    WIFI_ENCRYPTION_WEP_128,
    WIFI_ENCRYPTION_WEP_152,
    WIFI_ENCRYPTION_WPA_PSK_TKIP,
    WIFI_ENCRYPTION_WPA_PSK_AES,
    WIFI_ENCRYPTION_WPA2_PSK_TKIP,
    WIFI_ENCRYPTION_WPA2_PSK_AES,
    WIFI_ENCRYPTION_AMOUNT,
} wifi_encryption_type;

typedef struct {
    char SSID[0x20];
    u8   SSID_length;
    u8   encryption_type;
    u8   __padding1[2];
    char password_text[0x40];
    u8   password_key[0x20];
    u8   __padding2[0x20];
} __attribute__((packed)) sub_network_s;

typedef struct {
    u16 version;
    u16 crc_checksum;

    u8 network_enable;
    u8 editable_security;
    u8 __padding1[2];
    sub_network_s main_network;

    u8 multissid_enable;
    u8 multissid_type;
    u8 multissid_amount;
    u8 __padding2[1];
    sub_network_s multissid_networks[4];

    u8 enable_DHCP;
    u8 enable_autoDNS;
    u8 __padding3[2];

    u8 ip_address[4];
    u8 gateway_address[4];
    u8 subnet_mask[4];
    u8 dns_primary[4];
    u8 dns_secondary[4];

    u8 connection_test_success;
    u8 group_privacy_mode;
    u8 __padding4[2];
    u8 ip_to_use[4];
    u8 AP_MAC_address[6];
    u8 channel;
    u8 __padding5[1];

    u8  enable_proxy;
    u8  proxy_auth;
    u16 port_number;
    char proxy_url[0x30];
    u8   __padding6[0x34];
    char proxy_username[0x20];
    char proxy_password[0x20];

    u16 UPnP_value;
    u16 MTU_value;

    u8 __padding7[0x7EC];
} __attribute__((packed)) wifi_slot_s;

namespace wifi
{
    enum class Security { Open, WEP, WPA, WPA2 };

    struct Credentials {
        std::string ssid;
        std::string password;
        Security    security = Security::WPA2;
        bool        hidden   = false;
    };

    // Decoded, display-friendly view of a slot plus its raw bytes.
    struct Slot {
        bool        exists = false;
        bool        editableSecurity = false;
        bool        wps = false;              // only meaningful when !editableSecurity
        std::string ssid;
        std::string password;
        int         encryption = WIFI_ENCRYPTION_OPEN;
        wifi_slot_s raw{};
    };

    struct Backup {
        std::string displayName;  // alias if set, otherwise the network SSID
        std::string alias;        // user-provided label, may be empty
        std::time_t savedEpoch = 0;
        std::string savedDate;    // "YYYY-MM-DD HH:MM" derived from savedEpoch, may be empty
        std::string path;         // sdmc-relative path ("/3ds/WifiManager/...")
        Slot        slot;
    };

    const char* workingDir();

    // Console slots (index 0..2) -----------------------------------------
    bool   readConsoleSlot(int index, Slot& out);
    Result writeConsoleSlot(int index, const Slot& in);

    // Backup files ------------------------------------------------------
    std::vector<Backup> listBackups();
    bool   readBackupFile(const std::string& path, Slot& out,
                          std::string* aliasOut = nullptr, std::time_t* epochOut = nullptr);

    // Save `in` as a new backup. The on-disk file name is derived automatically
    // from the alias (or the SSID when no alias is given); a numeric suffix is
    // appended only to avoid a filesystem clash, so user-facing names may repeat
    // freely. The current time is recorded. Returns the written path, or "".
    std::string saveBackup(const Slot& in, const std::string& alias);

    // Rewrite an existing backup file in place (used by edit / rename).
    Result overwriteBackup(const std::string& path, const Slot& in,
                           const std::string& alias, std::time_t epoch);

    Result deleteBackupFile(const std::string& path);

    // Helpers ---------------------------------------------------------
    void        fixCrc(wifi_slot_s& s);
    void        decode(Slot& s);                       // fills ssid/password/... from raw
    std::string securityLabel(int encryption);
    Credentials credentialsFromSlot(const Slot& s);
    std::string formatEpoch(std::time_t t);            // "" when t == 0

    // Build a valid slot from plain credentials. If templ != nullptr and it
    // exists, its raw bytes are used as a base so every ancillary field stays
    // valid; otherwise a sane default block is synthesised.
    Slot fromCredentials(const Credentials& c, const Slot* templ);
}
