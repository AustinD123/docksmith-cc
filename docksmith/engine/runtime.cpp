#include "engine/commands.hpp"

#include <iostream>

namespace engine {

int Run(const std::string& stateRoot, const std::vector<std::string>& args) {
    (void)stateRoot;
    (void)args;
    std::cerr << "docksmith run: not implemented\n";
    return 1;
}

int Images(const std::string& stateRoot, const std::vector<std::string>& args) {
    (void)stateRoot;
    (void)args;
    std::cerr << "docksmith images: not implemented\n";
    return 1;
}

int RMI(const std::string& stateRoot, const std::vector<std::string>& args) {
    (void)stateRoot;
    (void)args;
    std::cerr << "docksmith rmi: not implemented\n";
    return 1;
}

}  // namespace engine
