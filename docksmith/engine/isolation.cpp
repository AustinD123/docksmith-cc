#include "engine/isolation.hpp"

#include <filesystem>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#endif

namespace engine {

IsolatedExecResult ExecuteInIsolatedRoot(const std::filesystem::path& rootDir,
                                         const std::string& workingDir,
                                         const std::vector<std::string>& envVars,
                                         const std::vector<std::string>& command) {
    if (command.empty()) {
        return {false, -1, "No command specified"};
    }

#if defined(__linux__)
    const pid_t pid = fork();
    if (pid < 0) {
        return {false, -1, std::strerror(errno)};
    }

    if (pid == 0) {
        if (chroot(rootDir.c_str()) != 0) {
            _exit(127);
        }

        const std::string effectiveWorkDir = workingDir.empty() ? "/" : workingDir;
        if (chdir(effectiveWorkDir.c_str()) != 0) {
            _exit(127);
        }

        for (const auto& env : envVars) {
            const auto pos = env.find('=');
            if (pos == std::string::npos) {
                continue;
            }
            const std::string key = env.substr(0, pos);
            const std::string value = env.substr(pos + 1U);
            setenv(key.c_str(), value.c_str(), 1);
        }

        std::vector<char*> argv;
        argv.reserve(command.size() + 1U);
        for (const auto& part : command) {
            argv.push_back(const_cast<char*>(part.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return {false, -1, std::strerror(errno)};
    }

    if (WIFEXITED(status)) {
        return {true, WEXITSTATUS(status), ""};
    }
    if (WIFSIGNALED(status)) {
        return {true, 128 + WTERMSIG(status), ""};
    }

    return {true, 1, ""};
#else
    (void)rootDir;
    (void)workingDir;
    (void)envVars;
    (void)command;
    return {false, -1, "Runtime isolation requires Linux (fork/chroot/exec)"};
#endif
}

}  // namespace engine
