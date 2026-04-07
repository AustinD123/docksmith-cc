#pragma once

#include <filesystem>
#include <string>

namespace util {

struct StateDirResult {
    bool ok;
    std::filesystem::path path;
    std::string error;
};

StateDirResult EnsureStateDirs();

}  // namespace util
