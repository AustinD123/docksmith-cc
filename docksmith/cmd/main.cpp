#include <filesystem>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "engine/commands.hpp"
#include "util/fs.hpp"

namespace {

constexpr const char* kAnsiReset = "\x1b[0m";
constexpr const char* kAnsiTitle = "\x1b[1;36m";
constexpr const char* kAnsiLabel = "\x1b[1;33m";
constexpr const char* kAnsiOk = "\x1b[1;32m";
constexpr const char* kAnsiErr = "\x1b[1;31m";

void EnableAnsiOnWindows() {
#if defined(_WIN32)
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(out, &mode)) {
        return;
    }

    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    (void)SetConsoleMode(out, mode);
#endif
}

std::string Trim(const std::string& text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) {
        --end;
    }

    return text.substr(start, end - start);
}

std::vector<std::string> SplitBySpaces(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream iss(text);
    std::string token;
    while (iss >> token) {
        out.push_back(token);
    }
    return out;
}

std::string PromptLine(const std::string& label) {
    std::cout << kAnsiLabel << label << kAnsiReset;
    std::string value;
    std::getline(std::cin, value);
    return Trim(value);
}

void PauseForEnter() {
    std::cout << "\nPress Enter to continue...";
    std::string ignore;
    std::getline(std::cin, ignore);
}

void ClearScreen() {
    std::cout << "\x1b[2J\x1b[H";
}

void PrintUiHeader() {
    std::cout << kAnsiTitle << "Docksmith Terminal UI" << kAnsiReset << "\n"
              << "----------------------------------------\n"
              << "1) Build image\n"
              << "2) Run image\n"
              << "3) List images\n"
              << "4) Remove image\n"
              << "5) Help\n"
              << "q) Quit\n\n";
}

void PrintUsage() {
    std::cout << "Usage: docksmith <command> [args]\n\n"
              << "Commands:\n"
              << "  build   Build an image from a Docksmithfile\n"
              << "  run     Run a container from an image\n"
              << "  images  List images in local state\n"
              << "  rmi     Remove an image\n"
              << "  ui      Open an interactive terminal UI\n";
}

int RunUi(const std::filesystem::path& stateRoot) {
    while (true) {
        ClearScreen();
        PrintUiHeader();

        const std::string choice = PromptLine("Select option: ");

        if (choice == "q" || choice == "Q") {
            std::cout << "Bye.\n";
            return 0;
        }

        if (choice == "1") {
            const std::string tag = PromptLine("Image (name:tag): ");
            std::string context = PromptLine("Context path (default .): ");
            if (context.empty()) {
                context = ".";
            }
            const std::string noCache = PromptLine("Use --no-cache? (y/N): ");

            if (tag.empty()) {
                std::cout << kAnsiErr << "Image tag is required.\n" << kAnsiReset;
                PauseForEnter();
                continue;
            }

            std::vector<std::string> args;
            if (noCache == "y" || noCache == "Y") {
                args.push_back("--no-cache");
            }
            args.push_back("-t");
            args.push_back(tag);
            args.push_back(context);

            const int code = engine::Build(stateRoot.string(), args);
            if (code == 0) {
                std::cout << kAnsiOk << "Build finished successfully.\n" << kAnsiReset;
            } else {
                std::cout << kAnsiErr << "Build failed. Exit code: " << code << "\n" << kAnsiReset;
            }
            PauseForEnter();
            continue;
        }

        if (choice == "2") {
            const std::string image = PromptLine("Image (name:tag): ");
            if (image.empty()) {
                std::cout << kAnsiErr << "Image is required.\n" << kAnsiReset;
                PauseForEnter();
                continue;
            }

            std::vector<std::string> args;
            while (true) {
                const std::string env = PromptLine("Add env KEY=VALUE (blank to stop): ");
                if (env.empty()) {
                    break;
                }
                args.push_back("-e");
                args.push_back(env);
            }

            args.push_back(image);

            const std::string overrideCmd = PromptLine("Override command (optional): ");
            const auto cmdParts = SplitBySpaces(overrideCmd);
            args.insert(args.end(), cmdParts.begin(), cmdParts.end());

            const int code = engine::Run(stateRoot.string(), args);
            if (code == 0) {
                std::cout << kAnsiOk << "Run finished successfully.\n" << kAnsiReset;
            } else {
                std::cout << kAnsiErr << "Run failed. Exit code: " << code << "\n" << kAnsiReset;
            }
            PauseForEnter();
            continue;
        }

        if (choice == "3") {
            const int code = engine::Images(stateRoot.string(), {});
            if (code != 0) {
                std::cout << kAnsiErr << "images failed. Exit code: " << code << "\n" << kAnsiReset;
            }
            PauseForEnter();
            continue;
        }

        if (choice == "4") {
            const std::string image = PromptLine("Image to remove (name:tag): ");
            if (image.empty()) {
                std::cout << kAnsiErr << "Image is required.\n" << kAnsiReset;
                PauseForEnter();
                continue;
            }
            const int code = engine::RMI(stateRoot.string(), {image});
            if (code == 0) {
                std::cout << kAnsiOk << "Image removed.\n" << kAnsiReset;
            } else {
                std::cout << kAnsiErr << "rmi failed. Exit code: " << code << "\n" << kAnsiReset;
            }
            PauseForEnter();
            continue;
        }

        if (choice == "5") {
            PrintUsage();
            PauseForEnter();
            continue;
        }

        std::cout << kAnsiErr << "Unknown option.\n" << kAnsiReset;
        PauseForEnter();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    EnableAnsiOnWindows();

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
    if (command == "ui") {
        return RunUi(stateRoot);
    }
    if (command == "help" || command == "-h" || command == "--help") {
        PrintUsage();
        return 0;
    }

    std::cerr << "docksmith: unknown command \"" << command << "\"\n";
    PrintUsage();
    return 1;
}
