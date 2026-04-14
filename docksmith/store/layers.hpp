#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "store/images.hpp"

namespace store {

struct CreateLayerResult {
    bool ok;
    std::string digest;
    std::int64_t size;
    std::string error;
};

CreateLayerResult CreateLayer(const std::filesystem::path& stateRoot,
                              const std::filesystem::path& deltaDir);
StoreResult ExtractLayers(const std::filesystem::path& stateRoot,
                          const std::vector<std::string>& layerList,
                          const std::filesystem::path& targetDir);
bool LayerExists(const std::filesystem::path& stateRoot, const std::string& digest);
StoreResult DeleteLayer(const std::filesystem::path& stateRoot, const std::string& digest);

}  // namespace store
