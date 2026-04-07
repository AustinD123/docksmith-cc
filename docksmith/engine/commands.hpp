#pragma once

#include <string>
#include <vector>

namespace engine {

int Build(const std::string& stateRoot, const std::vector<std::string>& args);
int Run(const std::string& stateRoot, const std::vector<std::string>& args);
int Images(const std::string& stateRoot, const std::vector<std::string>& args);
int RMI(const std::string& stateRoot, const std::vector<std::string>& args);

}  // namespace engine
