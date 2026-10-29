#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace instaseek::platform {

bool copyToClipboard(std::string_view utf8);
std::optional<std::string> readClipboard();

}
