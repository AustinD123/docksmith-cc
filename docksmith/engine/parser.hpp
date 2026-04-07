#pragma once

#include <string>
#include <vector>

namespace engine {

struct Instruction {
    std::string Type;
    std::string RawText;
    std::vector<std::string> Args;
    int LineNumber;
};

std::vector<Instruction> ParseDocksmithfile(const std::string& filePath);

}  // namespace engine