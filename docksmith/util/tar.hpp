#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace util {

bool CreateDeterministicTarFromDirectory(const std::filesystem::path& inputDir,
                                         std::vector<std::uint8_t>* outTar,
                                         std::string* outError);

bool ExtractTarBytes(const std::vector<std::uint8_t>& tarBytes,
                     const std::filesystem::path& targetDir,
                     std::string* outError);

bool ExtractTarFile(const std::filesystem::path& tarFile,
                    const std::filesystem::path& targetDir,
                    std::string* outError);

}  // namespace util
