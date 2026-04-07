#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/commands.hpp"
#include "util/fs.hpp"

namespace {

void PrintUsage() {
    std::cout << "Usage: docksmith <command> [args]\n\n"
              << "Commands:\n"
              << "  build   Build an image from a Docksmithfile\n"
              << "  run     Run a container from an image\n"
              << "  images  List images in local state\n"
              << "  rmi     Remove an image\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto stateRootResult = util::EnsureStateDirs();
    if (!stateRootResult.ok) {
        std::cerr << "docksmith: failed to initialize state directory: "
                  << stateRootResult.error << "\n";
        return 1;
    }

    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::string command = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const auto& stateRoot = stateRootResult.path;

    if (command == "build") {
        return engine::Build(stateRoot.string(), args);
    }
    if (command == "run") {
        return engine::Run(stateRoot.string(), args);
    }
    if (command == "images") {
        return engine::Images(stateRoot.string(), args);
    }
    if (command == "rmi") {
        return engine::RMI(stateRoot.string(), args);
    }
    if (command == "help" || command == "-h" || command == "--help") {
        PrintUsage();
        return 0;
    }

    std::cerr << "docksmith: unknown command \"" << command << "\"\n";
    PrintUsage();
    return 1;
}
