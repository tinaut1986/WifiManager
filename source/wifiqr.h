#pragma once
#include <string>
#include "wifi.h"

namespace wifiqr
{
    struct ParseResult {
        bool               ok = false;
        wifi::Credentials  cred;
        std::string        error;
        bool               fromStandardUri = false; // true: "WIFI:" ; false: plain text
    };

    // Accepts the standardised "WIFI:S:...;T:...;P:...;H:...;;" payload as well
    // as a plain-text fallback ("SSID" or "SSID\npassword").
    ParseResult parse(const std::string& payload);

    // Encode credentials back into the standard URI (handy for docs / tests).
    std::string encode(const wifi::Credentials& c);
}
