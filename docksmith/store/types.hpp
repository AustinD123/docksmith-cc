#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace store {

struct Layer {
    std::string digest;
    std::int64_t size;
    std::string createdBy;
};

struct ImageConfig {
    std::vector<std::string> env;
    std::vector<std::string> cmd;
    std::string workingDir;
};

struct ImageManifest {
    std::string name;
    std::string tag;
    std::string digest;
    std::string created;
    ImageConfig config;
    std::vector<Layer> layers;
};

struct CacheEntry {
    std::string key;
    std::string layerDigest;
};

}  // namespace store
