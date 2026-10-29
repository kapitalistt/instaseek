#include "platform/win.hpp"

#include "platform/unique_handle.hpp"

#include <windows.h>
#include <shellapi.h>

#include <climits>

namespace instaseek::platform {

bool isUsableHandle(NativeHandle handle) { return handle != nullptr && handle != INVALID_HANDLE_VALUE; }

void closeNativeHandle(NativeHandle handle) { CloseHandle(handle); }

std::wstring toWide(std::string_view utf8) {
    if (utf8.empty() || utf8.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int source_length = static_cast<int>(utf8.size());
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), source_length, nullptr, 0);
    if (length <= 0) return {};
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), source_length, out.data(), length);
    return out;
}

std::string toUtf8(std::wstring_view wide) {
    if (wide.empty() || wide.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int source_length = static_cast<int>(wide.size());
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), source_length, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), source_length, out.data(), length, nullptr, nullptr);
    return out;
}

Utf8ConsoleScope::Utf8ConsoleScope()
    : previous_input_code_page_(GetConsoleCP()), previous_output_code_page_(GetConsoleOutputCP()) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}

Utf8ConsoleScope::~Utf8ConsoleScope() {
    if (previous_input_code_page_ != 0) SetConsoleCP(previous_input_code_page_);
    if (previous_output_code_page_ != 0) SetConsoleOutputCP(previous_output_code_page_);
}

void hardenProcess() {
    SetDllDirectoryW(L"");
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
}

void deliverCtrlCAsKey() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (input == nullptr || input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode)) return;
    if ((mode & ENABLE_PROCESSED_INPUT) != 0) SetConsoleMode(input, mode & ~static_cast<DWORD>(ENABLE_PROCESSED_INPUT));
}

void setConsoleTitle(std::wstring_view title) {
    const std::wstring terminated(title);
    SetConsoleTitleW(terminated.c_str());
}

bool isProcessElevated() {
    HANDLE raw_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) return false;
    const UniqueHandle token(raw_token);
    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &returned)) return false;
    return elevation.TokenIsElevated != 0;
}

std::wstring executablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 8; ++attempt) {
        const DWORD capacity = static_cast<DWORD>(buffer.size());
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (length == 0) return {};
        if (length < capacity) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
    return {};
}

std::wstring quoteCommandLineArgument(std::wstring_view argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
        return std::wstring(argument);

    std::wstring quoted = L"\"";
    std::size_t pending_backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++pending_backslashes;
            continue;
        }
        if (c == L'"') {
            quoted.append(pending_backslashes * 2 + 1, L'\\');
        } else {
            quoted.append(pending_backslashes, L'\\');
        }
        pending_backslashes = 0;
        quoted.push_back(c);
    }
    quoted.append(pending_backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring joinCommandLine(const std::vector<std::wstring>& arguments) {
    std::wstring line;
    for (const auto& argument : arguments) {
        if (!line.empty()) line.push_back(L' ');
        line += quoteCommandLineArgument(argument);
    }
    return line;
}

ElevationResult relaunchElevated(const std::vector<std::wstring>& arguments) {
    const std::wstring executable = executablePath();
    if (executable.empty()) return ElevationResult::Failed;
    const std::wstring parameters = joinCommandLine(arguments);

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) return ElevationResult::Started;
    return GetLastError() == ERROR_CANCELLED ? ElevationResult::Declined : ElevationResult::Failed;
}

bool terminalLikelySupportsUnicodeIcons() {
    for (const wchar_t* variable : {L"WT_SESSION", L"TERM_PROGRAM", L"WEZTERM_EXECUTABLE", L"ConEmuANSI", L"ALACRITTY_LOG"})
        if (GetEnvironmentVariableW(variable, nullptr, 0) > 0) return true;
    return false;
}

std::wstring absolutePath(std::wstring_view path) {
    std::wstring cleaned(path);
    while (!cleaned.empty() && (cleaned.back() == L' ' || cleaned.back() == L'"')) cleaned.pop_back();
    while (!cleaned.empty() && (cleaned.front() == L' ' || cleaned.front() == L'"')) cleaned.erase(cleaned.begin());
    if (cleaned.empty()) return {};
    if (cleaned.rfind(L"\\\\?\\", 0) == 0) return cleaned;
    std::wstring buffer(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 4; ++attempt) {
        const DWORD length = GetFullPathNameW(cleaned.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length == 0) return cleaned;
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(static_cast<std::size_t>(length) + 1);
    }
    return cleaned;
}

std::wstring toExtendedPath(std::wstring_view path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) return std::wstring(path);
    const std::wstring absolute = absolutePath(path);
    if (absolute.rfind(L"\\\\?\\", 0) == 0) return absolute;
    if (absolute.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + absolute.substr(2);
    if (absolute.size() >= 2 && absolute[1] == L':') return L"\\\\?\\" + absolute;
    return absolute;
}

std::wstring withoutExtendedPrefix(std::wstring_view path) {
    if (path.rfind(L"\\\\?\\UNC\\", 0) == 0) return L"\\\\" + std::wstring(path.substr(8));
    if (path.rfind(L"\\\\?\\", 0) == 0) return std::wstring(path.substr(4));
    return std::wstring(path);
}

std::wstring joinPath(std::wstring_view directory, std::wstring_view name) {
    std::wstring joined(directory);
    if (!joined.empty() && joined.back() != L'\\') joined.push_back(L'\\');
    joined.append(name);
    return joined;
}

std::int64_t fileTimeTicks(unsigned long low, unsigned long high) {
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(high) << 32) | static_cast<std::uint64_t>(low));
}

std::int64_t nowUtcTicks() {
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    return fileTimeTicks(now.dwLowDateTime, now.dwHighDateTime);
}

std::int64_t localUtcOffsetTicks() {
    FILETIME utc{};
    GetSystemTimeAsFileTime(&utc);
    FILETIME local{};
    if (!FileTimeToLocalFileTime(&utc, &local)) return 0;
    return fileTimeTicks(local.dwLowDateTime, local.dwHighDateTime) - fileTimeTicks(utc.dwLowDateTime, utc.dwHighDateTime);
}

wchar_t systemDriveLetter() {
    wchar_t buffer[8] = {};
    const DWORD length = GetEnvironmentVariableW(L"SystemDrive", buffer, 8);
    if (length >= 1 && length < 8 && ((buffer[0] >= L'A' && buffer[0] <= L'Z') || (buffer[0] >= L'a' && buffer[0] <= L'z')))
        return static_cast<wchar_t>(buffer[0] & ~0x20);
    return L'C';
}

PathKind pathKind(std::wstring_view path) {
    if (path.empty()) return PathKind::Missing;
    const std::wstring extended = toExtendedPath(path);
    const DWORD attributes = GetFileAttributesW(extended.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return PathKind::Missing;
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? PathKind::Directory : PathKind::File;
}

std::string describeError(unsigned long error) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::string text;
    if (length > 0 && message != nullptr) text = toUtf8(std::wstring_view(message, length));
    if (message != nullptr) LocalFree(message);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '.'))
        text.pop_back();
    if (text.empty()) text = "Windows error";
    return text + " (" + std::to_string(error) + ")";
}

}
