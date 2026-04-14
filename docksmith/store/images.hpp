#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "store/types.hpp"

namespace store {

struct StoreResult {
    bool ok;
    std::string error;
};

struct LoadImageResult {
    bool ok;
    ImageManifest manifest;
    std::string error;
};

struct ImageSummary {
    std::string name;
    std::string tag;
    std::string id;
    std::string created;
};

struct ListImagesResult {
    bool ok;
    std::vector<ImageSummary> images;
    std::string error;
};

StoreResult SaveImage(const std::filesystem::path& stateRoot, ImageManifest manifest);
LoadImageResult LoadImage(const std::filesystem::path& stateRoot,
                          const std::string& name,
                          const std::string& tag);
ListImagesResult ListImages(const std::filesystem::path& stateRoot);
StoreResult DeleteImage(const std::filesystem::path& stateRoot,
                        const std::string& name,
                        const std::string& tag);

}  // namespace store
