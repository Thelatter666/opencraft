#include "opencraft/core/version.hpp"

namespace opencraft::core {

std::string version_string() {
    std::string result(kProjectName);
    result += ' ';
    result += std::to_string(kVersionMajor);
    result += '.';
    result += std::to_string(kVersionMinor);
    result += '.';
    result += std::to_string(kVersionPatch);
    return result;
}

} // namespace opencraft::core
