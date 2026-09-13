#pragma once

#include <string>
#include <string_view>

namespace opencraft::core {

inline constexpr std::string_view kProjectName = "OpenCraft";
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

// "OpenCraft 0.1.0"
std::string version_string();

} // namespace opencraft::core
