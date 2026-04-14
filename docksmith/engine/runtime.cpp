#include "engine/commands.hpp"

#include <iomanip>
#include <iostream>
#include <string>
#include <utility>

#include "store/images.hpp"

namespace engine {
namespace {

std::pair<std::string, std::string> ParseImageRef(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {"", ""};
    }

    if (args.size() >= 2U) {
        return {args[0], args[1]};
    }

    const std::string& image = args[0];
    const auto colon = image.find(':');
    if (colon == std::string::npos) {
        return {image, "latest"};
    }
    return {image.substr(0, colon), image.substr(colon + 1U)};
}

}  // namespace

int Run(const std::string& stateRoot, const std::vector<std::string>& args) {
    (void)stateRoot;
    (void)args;
    std::cerr << "docksmith run: not implemented\n";
    return 1;
}

int Images(const std::string& stateRoot, const std::vector<std::string>& args) {
    if (!args.empty()) {
        std::cerr << "Usage: docksmith images\n";
        return 1;
    }

    const auto listed = store::ListImages(stateRoot);
    if (!listed.ok) {
        std::cerr << listed.error << "\n";
        return 1;
    }

    std::cout << std::left << std::setw(20) << "NAME" << std::setw(12) << "TAG" << std::setw(14)
              << "ID" << "CREATED\n";
    for (const auto& image : listed.images) {
        std::cout << std::left << std::setw(20) << image.name << std::setw(12) << image.tag
                  << std::setw(14) << image.id << image.created << "\n";
    }
    return 0;
}

int RMI(const std::string& stateRoot, const std::vector<std::string>& args) {
    const auto [name, tag] = ParseImageRef(args);
    if (name.empty() || tag.empty()) {
        std::cerr << "Usage: docksmith rmi <name[:tag]> | <name> <tag>\n";
        return 1;
    }

    const auto removed = store::DeleteImage(stateRoot, name, tag);
    if (!removed.ok) {
        std::cerr << removed.error << "\n";
        return 1;
    }

    std::cout << "Removed image " << name << ":" << tag << "\n";
    return 0;
}

}  // namespace engine
