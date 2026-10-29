#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace instaseek::platform {

std::wstring toWide(std::string_view utf8);
std::string toUtf8(std::wstring_view wide);

class Utf8ConsoleScope {
public:
    Utf8ConsoleScope();
    ~Utf8ConsoleScope();

    Utf8ConsoleScope(const Utf8ConsoleScope&) = delete;
    Utf8ConsoleScope& operator=(const Utf8ConsoleScope&) = delete;

private:
    unsigned int previous_input_code_page_ = 0;
    unsigned int previous_output_code_page_ = 0;
};

void hardenProcess();
void setConsoleTitle(std::wstring_view title);
void deliverCtrlCAsKey();

bool isProcessElevated();
std::wstring executablePath();
std::wstring quoteCommandLineArgument(std::wstring_view argument);
std::wstring joinCommandLine(const std::vector<std::wstring>& arguments);

enum class ElevationResult { Started, Declined, Failed };
ElevationResult relaunchElevated(const std::vector<std::wstring>& arguments);

bool terminalLikelySupportsUnicodeIcons();

std::wstring absolutePath(std::wstring_view path);
std::wstring toExtendedPath(std::wstring_view path);
std::wstring withoutExtendedPrefix(std::wstring_view path);
std::wstring joinPath(std::wstring_view directory, std::wstring_view name);

std::int64_t fileTimeTicks(unsigned long low, unsigned long high);
std::int64_t nowUtcTicks();
std::int64_t localUtcOffsetTicks();
wchar_t systemDriveLetter();

enum class PathKind { Missing, File, Directory };
PathKind pathKind(std::wstring_view path);
std::string describeError(unsigned long error);

}
