#include "util/fs.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace util {
namespace {

std::filesystem::path ResolveHome() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home);
    }

    if (const char* userProfile = std::getenv("USERPROFILE");
        userProfile != nullptr && *userProfile != '\0') {
        return std::filesystem::path(userProfile);
    }

    return {};
}

}  // namespace

StateDirResult EnsureStateDirs() {
    const auto home = ResolveHome();
    if (home.empty()) {
        return {false, {}, "cannot resolve home directory from HOME or USERPROFILE"};
    }

    const auto root = home / ".docksmith";

    try {
        std::filesystem::create_directories(root);
        std::filesystem::create_directories(root / "images");
        std::filesystem::create_directories(root / "layers");
        std::filesystem::create_directories(root / "cache");
    } catch (const std::exception& ex) {
        return {false, {}, ex.what()};
    }

    return {true, root, ""};
}

}  // namespace util
