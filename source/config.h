#pragma once
#include "lang.h"
#include "theme.h"

namespace config
{
    struct Settings {
        lang::Id    langId  = lang::Id::Auto;
        theme::Type themeId = theme::Type::Dark;
    };

    extern Settings current;

    void load();
    void save();
    void apply();
}
