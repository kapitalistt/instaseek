#include "core/palette.hpp"
#include "platform/win.hpp"
#include "ui/app.hpp"
#include "ui/i18n.hpp"

#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace {

enum class IconPreference { Automatic, Unicode, Ascii };

struct CommandLine {
    instaseek::ui::AppOptions options;
    IconPreference icons = IconPreference::Automatic;
    std::optional<wchar_t> drive;
    bool show_help = false;
    bool show_version = false;
    std::string error;
};

void printUsage() {
    std::fputs(
        "InstaSeek " INSTASEEK_VERSION " - instant file name and file content search for Windows\n"
        "\n"
        "Usage: instaseek [options] [path]\n"
        "\n"
        "  path                   a file opens in the Viewer, a folder becomes the Content search location\n"
        "  --drive X              volume to index (default: the system drive)\n"
        "  --lang en|ru           interface language (F2 switches at runtime), default: en\n"
        "  --icons unicode|ascii  glyph set for icons and borders, default: detected from the terminal\n"
        "  --no-anim              disable all animations\n"
        "  --version              print the version and exit\n"
        "  --help                 print this help and exit\n"
        "\n"
        "Right click opens a menu: copy, paste, open, show in Explorer. Ctrl+V pastes, Ctrl+O browses.\n"
        "Name search: words = AND, a|b = OR. Content search: error|warning finds either word.\n"
        "Run as administrator for instant NTFS indexing through the MFT and USN journal.\n"
        "Without administrator rights InstaSeek falls back to a directory scan.\n"
        "Settings are never written to disk. InstaSeek only reads files and never modifies them.\n",
        stdout);
}

std::optional<wchar_t> parseDrive(const std::string& value) {
    if (value.empty() || value.size() > 3) return std::nullopt;
    const char letter = value[0];
    if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z'))) return std::nullopt;
    if (value.size() >= 2 && value[1] != ':') return std::nullopt;
    if (value.size() == 3 && value[2] != '\\') return std::nullopt;
    return static_cast<wchar_t>(letter & ~0x20);
}

CommandLine parseCommandLine(int argc, wchar_t** argv) {
    CommandLine result;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = instaseek::platform::toUtf8(argv[i]);
        const auto nextValue = [&]() -> std::optional<std::string> {
            if (i + 1 >= argc) return std::nullopt;
            return instaseek::platform::toUtf8(argv[++i]);
        };

        if (argument == "--help" || argument == "-h" || argument == "/?") {
            result.show_help = true;
        } else if (argument == "--version") {
            result.show_version = true;
        } else if (argument == "--no-anim") {
            result.options.animations = false;
        } else if (argument == "--lang") {
            const auto value = nextValue();
            std::optional<instaseek::ui::Language> language;
            if (value) language = instaseek::ui::parseLanguage(*value);
            if (!language) {
                result.error = "--lang expects 'en' or 'ru'";
                return result;
            }
            result.options.language = *language;
        } else if (argument == "--icons") {
            const auto value = nextValue();
            if (value && *value == "unicode") {
                result.icons = IconPreference::Unicode;
            } else if (value && *value == "ascii") {
                result.icons = IconPreference::Ascii;
            } else {
                result.error = "--icons expects 'unicode' or 'ascii'";
                return result;
            }
        } else if (argument == "--drive") {
            const auto value = nextValue();
            if (value) result.drive = parseDrive(*value);
            if (!result.drive) {
                result.error = "--drive expects a drive letter such as D or D:";
                return result;
            }
        } else if (!argument.empty() && argument[0] == '-') {
            result.error = "unknown option '" + argument + "' (see --help)";
            return result;
        } else if (result.options.initial_path.empty()) {
            result.options.initial_path = argv[i];
        } else {
            result.error = "only one path can be given";
            return result;
        }
    }
    return result;
}

}

int wmain(int argc, wchar_t** argv) {
    instaseek::platform::hardenProcess();
    const instaseek::platform::Utf8ConsoleScope utf8_console;

    CommandLine command_line = parseCommandLine(argc, argv);
    if (!command_line.error.empty()) {
        std::fprintf(stderr, "instaseek: %s\n", command_line.error.c_str());
        return 2;
    }
    if (command_line.show_help) {
        printUsage();
        return 0;
    }
    if (command_line.show_version) {
        std::fputs("InstaSeek " INSTASEEK_VERSION "\n", stdout);
        return 0;
    }

    auto& options = command_line.options;
    options.elevated = instaseek::platform::isProcessElevated();
    options.drive = command_line.drive ? *command_line.drive : instaseek::platform::systemDriveLetter();
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        options.relaunch_arguments.push_back(argument == options.initial_path ? instaseek::platform::absolutePath(argument) : argument);
    }
    switch (command_line.icons) {
        case IconPreference::Unicode: options.unicode_glyphs = true; break;
        case IconPreference::Ascii: options.unicode_glyphs = false; break;
        case IconPreference::Automatic: options.unicode_glyphs = instaseek::platform::terminalLikelySupportsUnicodeIcons(); break;
    }

    instaseek::platform::setConsoleTitle(L"InstaSeek");
    try {
        return instaseek::ui::runApp(options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "instaseek: fatal error: %s\n", error.what());
        return 1;
    }
}
