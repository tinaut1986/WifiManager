#pragma once
#include <string>
#include <vector>

// Tiny wrapper around Nayuki's qrcodegen: turns a string into a boolean module
// grid ready to be drawn.
namespace qrgen
{
    struct Code {
        int               size = 0;     // modules per side
        std::vector<char> mods;         // size*size, 1 = dark
        bool at(int x, int y) const { return mods[y * size + x] != 0; }
    };

    bool make(const std::string& text, Code& out);
}
