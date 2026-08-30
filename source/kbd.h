#pragma once
#include <string>

namespace kbd
{
    // Returns false if the user cancelled. When allowEmpty is true an empty
    // string is accepted (useful for optional fields such as an alias).
    bool text(std::string& out, const std::string& hint, const std::string& initial = "",
              unsigned maxLen = 96, bool password = false, bool allowEmpty = false);
}
