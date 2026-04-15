#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace store {

struct CacheLoadResult {
    bool ok;
    std::unordered_map<std::string, std::string> entries;
    std::string error;
};

struct CacheResult {
    bool ok;
    std::string error;
};

CacheLoadResult LoadCacheIndex(const std::filesystem::path& stateRoot);
CacheResult SaveCacheIndex(const std::filesystem::path& stateRoot,
                           const std::unordered_map<std::string, std::string>& entries);

}  // namespace store
