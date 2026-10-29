#pragma once

#include "core/palette.hpp"
#include "ui/i18n.hpp"

#include <string>
#include <vector>

namespace instaseek::ui {

struct AppOptions {
    Language language = Language::English;
    bool animations = true;
    bool unicode_glyphs = true;
    bool elevated = false;
    wchar_t drive = L'C';
    std::wstring initial_path;
    std::vector<std::wstring> relaunch_arguments;
};

int runApp(const AppOptions& options);

}
