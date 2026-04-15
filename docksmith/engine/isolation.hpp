#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace engine {

struct IsolatedExecResult {
    bool ok;
    int exitCode;
    std::string error;
};

IsolatedExecResult ExecuteInIsolatedRoot(const std::filesystem::path& rootDir,
                                         const std::string& workingDir,
                                         const std::vector<std::string>& envVars,
                                         const std::vector<std::string>& command);

}  // namespace engine
