#pragma once

#include <optional>
#include <string>

namespace instaseek::platform {

enum class PickKind { File, Folder };

std::optional<std::wstring> pickPath(PickKind kind, const std::wstring& start_location);
bool revealInExplorer(const std::wstring& path);

}
