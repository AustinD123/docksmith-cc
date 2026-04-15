#include "engine/commands.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/isolation.hpp"
#include "engine/parser.hpp"
#include "store/cache.hpp"
#include "store/images.hpp"
#include "store/layers.hpp"
#include "store/types.hpp"
#include "util/hash.hpp"

namespace engine {
namespace {

struct BuildArgs {
    bool ok = false;
    bool noCache = false;
    std::string imageName = "docksmith-image";
    std::string imageTag = "latest";
    std::filesystem::path context;
    std::string error;
};

std::pair<std::string, std::string> ParseImageRef(const std::string& image) {
    const auto pos = image.find(':');
    if (pos == std::string::npos) {
        return {image, "latest"};
    }
    return {image.substr(0, pos), image.substr(pos + 1U)};
}

BuildArgs ParseBuildArgs(const std::vector<std::string>& args) {
    BuildArgs out;
    std::vector<std::string> positional;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--no-cache") {
            out.noCache = true;
            continue;
        }
        if (args[i] == "-t") {
            if (i + 1U >= args.size()) {
                out.error = "Usage: docksmith build -t name:tag <context>";
                return out;
            }
            const auto [name, tag] = ParseImageRef(args[i + 1U]);
            if (name.empty() || tag.empty()) {
                out.error = "Invalid image tag";
                return out;
            }
            out.imageName = name;
            out.imageTag = tag;
            ++i;
            continue;
        }
        positional.push_back(args[i]);
    }

    if (positional.empty()) {
        out.context = std::filesystem::current_path();
    } else if (positional.size() == 1U) {
        out.context = positional[0];
    } else {
        out.error = "Usage: docksmith build -t name:tag <context>";
        return out;
    }

    if (!std::filesystem::exists(out.context) || !std::filesystem::is_directory(out.context)) {
        out.error = "Build context not found";
        return out;
    }

    out.ok = true;
    return out;
}

std::filesystem::path MakeTempDir(const std::string& prefix) {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (prefix + std::to_string(static_cast<unsigned long long>(now)));
}

std::string NormalizeContainerPath(const std::string& workingDir, const std::string& path) {
    const std::filesystem::path p(path);
    if (p.is_absolute()) {
        return p.lexically_normal().generic_string();
    }
    return (std::filesystem::path(workingDir) / p).lexically_normal().generic_string();
}

std::filesystem::path HostPathForContainerPath(const std::filesystem::path& rootDir,
                                               const std::string& containerPath) {
    std::string normalized = containerPath;
    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }
    return rootDir / std::filesystem::path(normalized);
}

bool EnsureDirectory(const std::filesystem::path& path, std::string* outError) {
    try {
        std::filesystem::create_directories(path);
        return true;
    } catch (const std::exception& ex) {
        *outError = ex.what();
        return false;
    }
}

std::string TrimLeft(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
        ++i;
    }
    return text.substr(i);
}

std::string ExtractRunCommand(const std::string& rawText) {
    if (rawText.size() <= 3U) {
        return "";
    }
    return TrimLeft(rawText.substr(3));
}

bool ParseCmdJsonArray(const std::string& rawText, std::vector<std::string>* outCmd) {
    std::string body = TrimLeft(rawText.substr(3));
    if (body.size() < 2U || body.front() != '[' || body.back() != ']') {
        return false;
    }

    outCmd->clear();
    std::size_t i = 1;
    while (i + 1U < body.size()) {
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i])) != 0) {
            ++i;
        }
        if (i < body.size() && body[i] == ']') {
            break;
        }
        if (i >= body.size() || body[i] != '"') {
            return false;
        }
        ++i;
        std::string value;
        while (i < body.size()) {
            const char ch = body[i++];
            if (ch == '"') {
                break;
            }
            if (ch == '\\') {
                if (i >= body.size()) {
                    return false;
                }
                const char esc = body[i++];
                if (esc == '"' || esc == '\\' || esc == '/') {
                    value.push_back(esc);
                } else if (esc == 'n') {
                    value.push_back('\n');
                } else if (esc == 'r') {
                    value.push_back('\r');
                } else if (esc == 't') {
                    value.push_back('\t');
                } else {
                    return false;
                }
                continue;
            }
            value.push_back(ch);
        }
        outCmd->push_back(value);

        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i])) != 0) {
            ++i;
        }
        if (i < body.size() && body[i] == ',') {
            ++i;
        }
    }
    return true;
}

std::string WildcardToRegex(const std::string& pattern) {
    std::string out = "^";
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (ch == '*') {
            if (i + 1U < pattern.size() && pattern[i + 1U] == '*') {
                out += ".*";
                ++i;
            } else {
                out += "[^/]*";
            }
            continue;
        }
        if (ch == '.' || ch == '+' || ch == '?' || ch == '(' || ch == ')' || ch == '[' || ch == ']' ||
            ch == '{' || ch == '}' || ch == '^' || ch == '$' || ch == '|') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out += "$";
    return out;
}

std::vector<std::filesystem::path> ResolveCopySources(const std::filesystem::path& context,
                                                      const std::string& srcPattern) {
    std::vector<std::filesystem::path> result;
    if (srcPattern.find('*') == std::string::npos) {
        const auto p = context / srcPattern;
        if (std::filesystem::exists(p)) {
            result.push_back(p);
        }
        return result;
    }

    const std::regex re(WildcardToRegex(std::filesystem::path(srcPattern).generic_string()));
    for (std::filesystem::recursive_directory_iterator it(context), end; it != end; ++it) {
        const auto rel = std::filesystem::relative(it->path(), context).generic_string();
        if (std::regex_match(rel, re)) {
            result.push_back(it->path());
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

void AppendSourceHashesForPath(const std::filesystem::path& context,
                               const std::filesystem::path& source,
                               std::vector<std::string>* outHashes,
                               std::string* outError) {
    try {
        if (std::filesystem::is_regular_file(source)) {
            std::string hash;
            if (!util::HashFile(source, &hash, outError)) {
                return;
            }
            outHashes->push_back(std::filesystem::relative(source, context).generic_string() + ":" + hash);
            return;
        }

        if (!std::filesystem::is_directory(source)) {
            return;
        }

        for (std::filesystem::recursive_directory_iterator it(source), end; it != end; ++it) {
            if (!it->is_regular_file()) {
                continue;
            }
            std::string hash;
            if (!util::HashFile(it->path(), &hash, outError)) {
                return;
            }
            outHashes->push_back(std::filesystem::relative(it->path(), context).generic_string() + ":" + hash);
        }
    } catch (const std::exception& ex) {
        *outError = ex.what();
    }
}

bool CollectCopySourceHashes(const std::filesystem::path& context,
                             const std::vector<std::filesystem::path>& sources,
                             std::vector<std::string>* outHashes,
                             std::string* outError) {
    outHashes->clear();
    for (const auto& source : sources) {
        AppendSourceHashesForPath(context, source, outHashes, outError);
        if (!outError->empty()) {
            return false;
        }
    }
    std::sort(outHashes->begin(), outHashes->end());
    return true;
}

bool ReadLayerSize(const std::filesystem::path& stateRoot,
                   const std::string& digest,
                   std::int64_t* outSize,
                   std::string* outError) {
    try {
        const auto path = stateRoot / "layers" / (digest + ".tar");
        *outSize = static_cast<std::int64_t>(std::filesystem::file_size(path));
        return true;
    } catch (const std::exception& ex) {
        *outError = ex.what();
        return false;
    }
}

bool CopyIntoRoot(const std::filesystem::path& src,
                  const std::filesystem::path& context,
                  const std::filesystem::path& rootDir,
                  const std::string& destContainerPath,
                  std::string* outError) {
    const auto destRoot = HostPathForContainerPath(rootDir, destContainerPath);

    try {
        if (std::filesystem::is_regular_file(src)) {
            if ((!destContainerPath.empty() && destContainerPath.back() == '/') ||
                (std::filesystem::exists(destRoot) && std::filesystem::is_directory(destRoot))) {
                std::filesystem::create_directories(destRoot);
                std::filesystem::copy_file(src, destRoot / src.filename(),
                                           std::filesystem::copy_options::overwrite_existing);
            } else {
                std::filesystem::create_directories(destRoot.parent_path());
                std::filesystem::copy_file(src, destRoot, std::filesystem::copy_options::overwrite_existing);
            }
            return true;
        }

        if (std::filesystem::is_directory(src)) {
            const auto target = destRoot / src.filename();
            std::filesystem::create_directories(target);
            for (std::filesystem::recursive_directory_iterator it(src), end; it != end; ++it) {
                const auto rel = std::filesystem::relative(it->path(), src);
                const auto outPath = target / rel;
                if (it->is_directory()) {
                    std::filesystem::create_directories(outPath);
                } else if (it->is_regular_file()) {
                    std::filesystem::create_directories(outPath.parent_path());
                    std::filesystem::copy_file(it->path(), outPath,
                                               std::filesystem::copy_options::overwrite_existing);
                }
            }
            return true;
        }

        (void)context;
        *outError = "Unsupported COPY source";
        return false;
    } catch (const std::exception& ex) {
        *outError = ex.what();
        return false;
    }
}

using SnapshotMap = std::map<std::string, std::string>;

bool CaptureSnapshot(const std::filesystem::path& rootDir, SnapshotMap* out, std::string* outError) {
    out->clear();
    try {
        if (!std::filesystem::exists(rootDir)) {
            return true;
        }
        for (std::filesystem::recursive_directory_iterator it(rootDir), end; it != end; ++it) {
            const auto rel = std::filesystem::relative(it->path(), rootDir).generic_string();
            if (rel.empty()) {
                continue;
            }

            const auto st = it->symlink_status();
            if (std::filesystem::is_symlink(st)) {
                (*out)[rel] = "L:" + std::filesystem::read_symlink(it->path()).generic_string();
            } else if (std::filesystem::is_regular_file(st)) {
                std::string hash;
                std::string hashError;
                if (!util::HashFile(it->path(), &hash, &hashError)) {
                    *outError = hashError;
                    return false;
                }
                (*out)[rel] = "F:" + hash;
            }
        }
    } catch (const std::exception& ex) {
        *outError = ex.what();
        return false;
    }
    return true;
}

bool BuildDeltaDirectory(const std::filesystem::path& rootDir,
                        const SnapshotMap& before,
                        const SnapshotMap& after,
                        const std::filesystem::path& deltaDir,
                        std::string* outError) {
    try {
        std::filesystem::create_directories(deltaDir);
        for (const auto& kv : after) {
            const auto it = before.find(kv.first);
            if (it != before.end() && it->second == kv.second) {
                continue;
            }

            const auto srcPath = rootDir / std::filesystem::path(kv.first);
            const auto outPath = deltaDir / std::filesystem::path(kv.first);
            std::filesystem::create_directories(outPath.parent_path());

            if (kv.second.rfind("L:", 0) == 0U) {
                std::error_code ec;
                std::filesystem::create_symlink(std::filesystem::read_symlink(srcPath), outPath, ec);
                if (ec) {
                    *outError = ec.message();
                    return false;
                }
            } else {
                std::filesystem::copy_file(srcPath, outPath, std::filesystem::copy_options::overwrite_existing);
            }
        }
    } catch (const std::exception& ex) {
        *outError = ex.what();
        return false;
    }
    return true;
}

std::string ComputeCacheKey(const std::string& previousDigest,
                            const Instruction& instruction,
                            const std::string& workingDir,
                            const std::map<std::string, std::string>& envVars,
                            const std::vector<std::string>& sourceHashes) {
    std::string payload;
    payload += previousDigest;
    payload += "\n";
    payload += instruction.RawText;
    payload += "\n";
    payload += workingDir;
    payload += "\n";

    for (const auto& env : envVars) {
        payload += env.first + "=" + env.second + "\n";
    }
    for (const auto& hash : sourceHashes) {
        payload += hash + "\n";
    }

    return util::HashString(payload);
}

std::vector<std::string> EnvMapToList(const std::map<std::string, std::string>& envVars) {
    std::vector<std::string> out;
    out.reserve(envVars.size());
    for (const auto& kv : envVars) {
        out.push_back(kv.first + "=" + kv.second);
    }
    return out;
}

}  // namespace

int Build(const std::string& stateRoot, const std::vector<std::string>& args) {
    const auto buildArgs = ParseBuildArgs(args);
    if (!buildArgs.ok) {
        std::cerr << buildArgs.error << "\n";
        return 1;
    }

    const auto docksmithfile = buildArgs.context / "Docksmithfile";
    std::vector<Instruction> instructions;
    try {
        instructions = ParseDocksmithfile(docksmithfile.string());
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    if (instructions.empty() || instructions[0].Type != "FROM" || instructions[0].Args.empty()) {
        std::cerr << "First instruction must be FROM <image:tag>\n";
        return 1;
    }

    std::unordered_map<std::string, std::string> cacheIndex;
    if (!buildArgs.noCache) {
        auto cacheLoaded = store::LoadCacheIndex(stateRoot);
        if (!cacheLoaded.ok) {
            std::cerr << cacheLoaded.error << "\n";
            return 1;
        }
        cacheIndex = std::move(cacheLoaded.entries);
    }

    std::filesystem::path workRoot = MakeTempDir("docksmith-build-");
    std::string dirError;
    if (!EnsureDirectory(workRoot, &dirError)) {
        std::cerr << dirError << "\n";
        return 1;
    }
    const auto cleanup = [&workRoot]() {
        std::error_code ec;
        std::filesystem::remove_all(workRoot, ec);
    };

    store::ImageManifest manifest;
    manifest.name = buildArgs.imageName;
    manifest.tag = buildArgs.imageTag;

    std::map<std::string, std::string> envVars;
    std::string currentWorkDir = "/";
    std::string previousDigest;

    bool cascadeMiss = false;

    for (std::size_t i = 0; i < instructions.size(); ++i) {
        const auto& instruction = instructions[i];
        std::cout << "Step " << (i + 1U) << "/" << instructions.size() << " : " << instruction.RawText;

        if (instruction.Type == "FROM") {
            const auto [baseName, baseTag] = ParseImageRef(instruction.Args[0]);
            if (baseName == "scratch") {
                previousDigest = "scratch";
                std::cout << "\n";
                continue;
            }

            auto base = store::LoadImage(stateRoot, baseName, baseTag);
            if (!base.ok) {
                cleanup();
                std::cerr << "\nBase image " << baseName << ":" << baseTag << " not found\n";
                return 1;
            }

            std::vector<std::string> digests;
            for (const auto& layer : base.manifest.layers) {
                manifest.layers.push_back(layer);
                digests.push_back(layer.digest);
            }
            const auto extracted = store::ExtractLayers(stateRoot, digests, workRoot);
            if (!extracted.ok) {
                cleanup();
                std::cerr << "\n" << extracted.error << "\n";
                return 1;
            }

            manifest.config = base.manifest.config;
            currentWorkDir = manifest.config.workingDir.empty() ? "/" : manifest.config.workingDir;
            for (const auto& e : manifest.config.env) {
                const auto eq = e.find('=');
                if (eq != std::string::npos) {
                    envVars[e.substr(0, eq)] = e.substr(eq + 1U);
                }
            }
            previousDigest = base.manifest.digest;
            std::cout << "\n";
            continue;
        }

        if (instruction.Type == "WORKDIR") {
            if (instruction.Args.empty()) {
                cleanup();
                std::cerr << "\nInvalid WORKDIR\n";
                return 1;
            }
            currentWorkDir = NormalizeContainerPath(currentWorkDir, instruction.Args[0]);
            std::string dirError;
            if (!EnsureDirectory(HostPathForContainerPath(workRoot, currentWorkDir), &dirError)) {
                cleanup();
                std::cerr << "\n" << dirError << "\n";
                return 1;
            }
            manifest.config.workingDir = currentWorkDir;
            std::cout << "\n";
            continue;
        }

        if (instruction.Type == "ENV") {
            if (instruction.Args.empty()) {
                cleanup();
                std::cerr << "\nInvalid ENV\n";
                return 1;
            }
            const auto eq = instruction.Args[0].find('=');
            if (eq == std::string::npos) {
                cleanup();
                std::cerr << "\nInvalid ENV\n";
                return 1;
            }
            envVars[instruction.Args[0].substr(0, eq)] = instruction.Args[0].substr(eq + 1U);
            manifest.config.env = EnvMapToList(envVars);
            std::cout << "\n";
            continue;
        }

        if (instruction.Type == "CMD") {
            std::vector<std::string> cmd;
            if (!ParseCmdJsonArray(instruction.RawText, &cmd)) {
                cleanup();
                std::cerr << "\nInvalid CMD JSON array\n";
                return 1;
            }
            manifest.config.cmd = cmd;
            std::cout << "\n";
            continue;
        }

        if (instruction.Type != "COPY" && instruction.Type != "RUN") {
            cleanup();
            std::cerr << "\nUnsupported instruction\n";
            return 1;
        }

        std::string dirError;
        if (!EnsureDirectory(HostPathForContainerPath(workRoot, currentWorkDir), &dirError)) {
            cleanup();
            std::cerr << "\n" << dirError << "\n";
            return 1;
        }

        std::vector<std::filesystem::path> sources;
        std::string copyDest;
        std::vector<std::string> sourceHashes;

        if (instruction.Type == "COPY") {
            if (instruction.Args.size() < 2U) {
                cleanup();
                std::cerr << "\nInvalid COPY\n";
                return 1;
            }
            sources = ResolveCopySources(buildArgs.context, instruction.Args[0]);
            if (sources.empty()) {
                cleanup();
                std::cerr << "\nCOPY source not found\n";
                return 1;
            }
            copyDest = NormalizeContainerPath(currentWorkDir, instruction.Args[1]);
            std::string hashError;
            if (!CollectCopySourceHashes(buildArgs.context, sources, &sourceHashes, &hashError)) {
                cleanup();
                std::cerr << "\n" << hashError << "\n";
                return 1;
            }
        }

        const auto key = ComputeCacheKey(previousDigest, instruction, currentWorkDir, envVars, sourceHashes);
        const bool canHit = !buildArgs.noCache && !cascadeMiss && (cacheIndex.find(key) != cacheIndex.end()) &&
                            store::LayerExists(stateRoot, cacheIndex[key]);

        const auto stepStart = std::chrono::steady_clock::now();
        if (canHit) {
            const std::string layerDigest = cacheIndex[key];
            auto apply = store::ExtractLayers(stateRoot, std::vector<std::string>{layerDigest}, workRoot);
            if (!apply.ok) {
                cleanup();
                std::cerr << "\n" << apply.error << "\n";
                return 1;
            }
            std::int64_t layerSize = 0;
            std::string sizeError;
            if (!ReadLayerSize(stateRoot, layerDigest, &layerSize, &sizeError)) {
                cleanup();
                std::cerr << "\n" << sizeError << "\n";
                return 1;
            }
            manifest.layers.push_back({layerDigest, layerSize, instruction.RawText});
            previousDigest = layerDigest;

            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - stepStart);
            std::cout << " [CACHE HIT] " << std::fixed << std::setprecision(2) << elapsed.count() << "s\n";
            continue;
        }

        cascadeMiss = true;

        SnapshotMap before;
        std::string snapshotError;
        if (!CaptureSnapshot(workRoot, &before, &snapshotError)) {
            cleanup();
            std::cerr << "\n" << snapshotError << "\n";
            return 1;
        }

        if (instruction.Type == "COPY") {
            for (const auto& src : sources) {
                std::string copyError;
                if (!CopyIntoRoot(src, buildArgs.context, workRoot, copyDest, &copyError)) {
                    cleanup();
                    std::cerr << "\n" << copyError << "\n";
                    return 1;
                }
            }
        } else if (instruction.Type == "RUN") {
            const std::string runCommand = ExtractRunCommand(instruction.RawText);
            auto runRes = ExecuteInIsolatedRoot(workRoot, currentWorkDir, EnvMapToList(envVars),
                                                std::vector<std::string>{"/bin/sh", "-c", runCommand});
            if (!runRes.ok) {
                cleanup();
                std::cerr << "\n" << runRes.error << "\n";
                return 1;
            }
            if (runRes.exitCode != 0) {
                cleanup();
                std::cerr << "\nRUN failed with exit code " << runRes.exitCode << "\n";
                return runRes.exitCode;
            }
        }

        SnapshotMap after;
        if (!CaptureSnapshot(workRoot, &after, &snapshotError)) {
            cleanup();
            std::cerr << "\n" << snapshotError << "\n";
            return 1;
        }

        const auto deltaDir = MakeTempDir("docksmith-delta-");
        if (!BuildDeltaDirectory(workRoot, before, after, deltaDir, &snapshotError)) {
            cleanup();
            std::error_code ec;
            std::filesystem::remove_all(deltaDir, ec);
            std::cerr << "\n" << snapshotError << "\n";
            return 1;
        }

        const auto createdLayer = store::CreateLayer(stateRoot, deltaDir);
        {
            std::error_code ec;
            std::filesystem::remove_all(deltaDir, ec);
        }
        if (!createdLayer.ok) {
            cleanup();
            std::cerr << "\n" << createdLayer.error << "\n";
            return 1;
        }

        manifest.layers.push_back({createdLayer.digest, createdLayer.size, instruction.RawText});
        previousDigest = createdLayer.digest;
        if (!buildArgs.noCache) {
            cacheIndex[key] = createdLayer.digest;
        }

        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - stepStart);
        std::cout << " [CACHE MISS] " << std::fixed << std::setprecision(2) << elapsed.count() << "s\n";
    }

    auto existing = store::LoadImage(stateRoot, buildArgs.imageName, buildArgs.imageTag);
    if (existing.ok) {
        manifest.created = existing.manifest.created;
    }

    manifest.config.workingDir = currentWorkDir;
    manifest.config.env = EnvMapToList(envVars);

    const auto saved = store::SaveImage(stateRoot, manifest);
    if (!saved.ok) {
        cleanup();
        std::cerr << saved.error << "\n";
        return 1;
    }

    if (!buildArgs.noCache) {
        const auto cacheSaved = store::SaveCacheIndex(stateRoot, cacheIndex);
        if (!cacheSaved.ok) {
            cleanup();
            std::cerr << cacheSaved.error << "\n";
            return 1;
        }
    }

    auto finalImage = store::LoadImage(stateRoot, buildArgs.imageName, buildArgs.imageTag);
    cleanup();

    if (!finalImage.ok) {
        std::cerr << finalImage.error << "\n";
        return 1;
    }

    std::string digest = finalImage.manifest.digest;
    if (digest.rfind("sha256:", 0) == 0U) {
        digest = digest.substr(7);
    }
    if (digest.size() > 12U) {
        digest = digest.substr(0, 12U);
    }

    std::cout << "Successfully built sha256:" << digest << " " << buildArgs.imageName << ":"
              << buildArgs.imageTag << "\n";
    return 0;
}

}  // namespace engine
