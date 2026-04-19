#include "engine/commands.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "engine/isolation.hpp"
#include "store/images.hpp"
#include "store/layers.hpp"

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

std::vector<std::string> ParseEnvVars(const std::vector<std::string>& envFromManifest,
                                      const std::vector<std::string>& envOverrides) {
    std::vector<std::string> merged = envFromManifest;
    for (const auto& item : envOverrides) {
        const auto eq = item.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = item.substr(0, eq);
        bool replaced = false;
        for (auto& existing : merged) {
            if (existing.rfind(key + "=", 0) == 0U) {
                existing = item;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            merged.push_back(item);
        }
    }
    return merged;
}

std::filesystem::path MakeTempDir(const std::string& prefix) {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (prefix + std::to_string(static_cast<unsigned long long>(now)));
}

}  // namespace

int Run(const std::string& stateRoot, const std::vector<std::string>& args) {
    std::vector<std::string> envOverrides;
    std::vector<std::string> positional;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-e") {
            if (i + 1U >= args.size()) {
                std::cerr << "Usage: docksmith run [-e KEY=VALUE ...] <image[:tag]> [command...]\n";
                return 1;
            }
            envOverrides.push_back(args[i + 1U]);
            ++i;
            continue;
        }
        positional.push_back(args[i]);
    }

    if (positional.empty()) {
        std::cerr << "Usage: docksmith run [-e KEY=VALUE ...] <image[:tag]> [command...]\n";
        return 1;
    }

    const auto imageRef = std::vector<std::string>{positional[0]};
    const auto [name, tag] = ParseImageRef(imageRef);
    if (name.empty() || tag.empty()) {
        std::cerr << "Invalid image reference\n";
        return 1;
    }

    auto loaded = store::LoadImage(stateRoot, name, tag);
    if (!loaded.ok) {
        std::cerr << loaded.error << "\n";
        return 1;
    }

    const auto tempRoot = MakeTempDir("docksmith-run-");
    try {
        std::filesystem::create_directories(tempRoot);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    const auto cleanup = [&tempRoot]() {
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
    };

    const auto extracted = store::ExtractLayers(stateRoot, [&loaded]() {
        std::vector<std::string> digests;
        digests.reserve(loaded.manifest.layers.size());
        for (const auto& layer : loaded.manifest.layers) {
            digests.push_back(layer.digest);
        }
        return digests;
    }(), tempRoot);
    if (!extracted.ok) {
        cleanup();
        std::cerr << extracted.error << "\n";
        return 1;
    }

    std::vector<std::string> command;
    if (positional.size() > 1U) {
        command.assign(positional.begin() + 1U, positional.end());
    } else if (!loaded.manifest.config.cmd.empty()) {
        command = loaded.manifest.config.cmd;
    } else {
        cleanup();
        std::cerr << "No CMD defined in image and no runtime command override provided\n";
        return 1;
    }

    const auto env = ParseEnvVars(loaded.manifest.config.env, envOverrides);
    const auto execResult = ExecuteInIsolatedRoot(tempRoot, loaded.manifest.config.workingDir, env, command);

    cleanup();

    if (!execResult.ok) {
        std::cerr << execResult.error << "\n";
        return 1;
    }

    std::cout << "Exit code: " << execResult.exitCode << "\n";
    return execResult.exitCode;
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
