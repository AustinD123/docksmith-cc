#include "engine/commands.hpp"

#include <iostream>

namespace engine {

int Build(const std::string& stateRoot, const std::vector<std::string>& args) {
    (void)stateRoot;
    (void)args;
    std::cerr << "docksmith build: not implemented\n";
    return 1;
}

}  // namespace engine
