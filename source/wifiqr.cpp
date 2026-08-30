#include "wifiqr.h"
#include <cctype>
#include <algorithm>

namespace
{
    std::string toUpper(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        return s;
    }

    // Unescape the MECARD-style escaping used by the WIFI: scheme.
    std::string unescape(const std::string& in)
    {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == '\\' && i + 1 < in.size()) { out += in[++i]; }
            else                                    { out += in[i]; }
        }
        return out;
    }

    std::string escape(const std::string& in)
    {
        std::string out;
        for (char c : in)
        {
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"')
                out += '\\';
            out += c;
        }
        return out;
    }

    // Split "WIFI:" body into key/value pairs, honouring backslash escapes so an
    // escaped ';' inside a password does not terminate the field.
    void splitFields(const std::string& body,
                     std::string& S, std::string& T, std::string& P, std::string& H)
    {
        size_t i = 0;
        while (i < body.size())
        {
            // key is up to the first unescaped ':'
            std::string key;
            while (i < body.size() && body[i] != ':')
                key += body[i++];
            if (i < body.size()) ++i; // skip ':'

            std::string val;
            while (i < body.size())
            {
                char c = body[i];
                if (c == '\\' && i + 1 < body.size()) { val += c; val += body[i + 1]; i += 2; continue; }
                if (c == ';') { ++i; break; }
                val += c;
                ++i;
            }

            std::string k = toUpper(key);
            if      (k == "S") S = val;
            else if (k == "T") T = val;
            else if (k == "P") P = val;
            else if (k == "H") H = val;
        }
    }
}

namespace wifiqr
{

ParseResult parse(const std::string& payload)
{
    ParseResult res;

    std::string trimmed = payload;
    while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == '\n' || trimmed.back() == ' '))
        trimmed.pop_back();

    if (toUpper(trimmed.substr(0, 5)) == "WIFI:")
    {
        res.fromStandardUri = true;
        std::string body = trimmed.substr(5);
        // drop the trailing ";;" terminator if present
        while (!body.empty() && body.back() == ';') body.pop_back();

        std::string S, T, P, H;
        splitFields(body, S, T, P, H);

        res.cred.ssid     = unescape(S);
        res.cred.password = unescape(P);

        std::string t = toUpper(unescape(T));
        if      (t == "NOPASS" || t.empty()) res.cred.security = wifi::Security::Open;
        else if (t == "WEP")                 res.cred.security = wifi::Security::WEP;
        else if (t == "WPA")                 res.cred.security = wifi::Security::WPA;
        else                                 res.cred.security = wifi::Security::WPA2; // WPA2, WPA2-EAP, WPA3, SAE...

        std::string h = toUpper(unescape(H));
        res.cred.hidden = (h == "TRUE" || h == "1");

        if (res.cred.ssid.empty()) { res.error = "QR has no SSID"; return res; }
        res.ok = true;
        return res;
    }

    // ---- plain text fallback -------------------------------------------------
    size_t nl = trimmed.find_first_of("\r\n");
    if (nl != std::string::npos)
    {
        res.cred.ssid = trimmed.substr(0, nl);
        std::string rest = trimmed.substr(nl + 1);
        while (!rest.empty() && (rest.front() == '\r' || rest.front() == '\n'))
            rest.erase(rest.begin());
        res.cred.password = rest;
        res.cred.security = rest.empty() ? wifi::Security::Open : wifi::Security::WPA2;
    }
    else
    {
        res.cred.ssid = trimmed;
        res.cred.security = wifi::Security::Open;
    }

    if (res.cred.ssid.empty()) { res.error = "Empty QR payload"; return res; }
    res.ok = true;
    return res;
}

std::string encode(const wifi::Credentials& c)
{
    const char* t = "WPA2";
    switch (c.security)
    {
        case wifi::Security::Open: t = "nopass"; break;
        case wifi::Security::WEP:  t = "WEP";    break;
        case wifi::Security::WPA:  t = "WPA";    break;
        case wifi::Security::WPA2: t = "WPA2";   break;
    }
    std::string out = "WIFI:S:" + escape(c.ssid) + ";T:" + std::string(t) + ";";
    if (c.security != wifi::Security::Open)
        out += "P:" + escape(c.password) + ";";
    if (c.hidden)
        out += "H:true;";
    out += ";";
    return out;
}

} // namespace wifiqr
