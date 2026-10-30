#pragma once

#include <string_view>

namespace toy::style::detail {

// The embedded user-agent style sheet. It is parsed with toy::css on every
// ComputeStyles call, like any other style sheet.
std::string_view UserAgentStyleSheet();

}  // namespace toy::style::detail
