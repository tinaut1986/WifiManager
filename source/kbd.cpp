#include "kbd.h"
#include <3ds.h>
#include <vector>

namespace kbd
{

bool text(std::string& out, const std::string& hint, const std::string& initial,
          unsigned maxLen, bool password, bool allowEmpty)
{
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, maxLen);
    swkbdSetHintText(&swkbd, hint.c_str());
    if (!allowEmpty)
        swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (!initial.empty())
        swkbdSetInitialText(&swkbd, initial.c_str());
    if (password)
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);

    std::vector<char> buf(maxLen + 1, '\0');
    SwkbdButton btn = swkbdInputText(&swkbd, buf.data(), buf.size());
    if (btn != SWKBD_BUTTON_CONFIRM)
        return false;

    out.assign(buf.data());
    return true;
}

} // namespace kbd
