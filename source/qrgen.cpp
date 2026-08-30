#include "qrgen.h"

extern "C" {
#include "qrcodegen/qrcodegen.h"
}

namespace qrgen
{

bool make(const std::string& text, Code& out)
{
    std::vector<uint8_t> qr(qrcodegen_BUFFER_LEN_MAX);
    std::vector<uint8_t> tmp(qrcodegen_BUFFER_LEN_MAX);

    bool ok = qrcodegen_encodeText(text.c_str(), tmp.data(), qr.data(),
                                   qrcodegen_Ecc_MEDIUM,
                                   qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                   qrcodegen_Mask_AUTO, true);
    if (!ok) return false;

    int n = qrcodegen_getSize(qr.data());
    out.size = n;
    out.mods.assign(static_cast<size_t>(n) * n, 0);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            out.mods[y * n + x] = qrcodegen_getModule(qr.data(), x, y) ? 1 : 0;
    return true;
}

} // namespace qrgen
