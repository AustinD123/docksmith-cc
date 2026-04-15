#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace util {

std::string HashBytes(const std::vector<std::uint8_t>& bytes);
std::string HashString(const std::string& text);
bool HashFile(const std::filesystem::path& path, std::string* outHash, std::string* outError);

}  // namespace util
