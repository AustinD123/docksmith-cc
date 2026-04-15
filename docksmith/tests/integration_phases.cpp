#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "engine/commands.hpp"
#include "engine/parser.hpp"
#include "store/cache.hpp"
#include "store/images.hpp"
#include "store/layers.hpp"
#include "store/types.hpp"

namespace {

struct TestContext {
    int passed = 0;
    int failed = 0;
};

void Check(TestContext* ctx, const bool cond, const std::string& message) {
    if (cond) {
        ++ctx->passed;
        return;
    }
    ++ctx->failed;
    std::cerr << "FAIL: " << message << "\n";
}

std::filesystem::path MakeUniquePath(const std::string& name) {
    const auto base = std::filesystem::temp_directory_path();
    const auto unique = name + "_" + std::to_string(static_cast<unsigned long long>(
                                     std::filesystem::file_time_type::clock::now().time_since_epoch().count()));
    return base / unique;
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << contents;
    return static_cast<bool>(out);
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return content;
}

}  // namespace

int main() {
    TestContext t;

    const auto root = MakeUniquePath("docksmith_phase_test");
    const auto stateRoot = root / "state";
    const auto workRoot = root / "work";

    std::error_code ec;
    std::filesystem::create_directories(stateRoot / "images", ec);
    std::filesystem::create_directories(stateRoot / "layers", ec);
    std::filesystem::create_directories(stateRoot / "cache", ec);
    std::filesystem::create_directories(workRoot, ec);

    // Phase 1: parser.
    const auto dockerfile = workRoot / "Docksmithfile";
    const std::string dockerfileText =
        "FROM scratch\n"
        "WORKDIR /app\n"
        "ENV FOO=bar\n"
        "COPY . /app\n"
        "RUN echo hello\n"
        "CMD /bin/sh\n";
    Check(&t, WriteTextFile(dockerfile, dockerfileText), "write Docksmithfile");

    try {
        const auto instructions = engine::ParseDocksmithfile(dockerfile.string());
        Check(&t, instructions.size() == 6U, "phase 1 parse instruction count");
        Check(&t, instructions[0].Type == "FROM", "phase 1 FROM parsed");
        Check(&t, instructions[5].Type == "CMD", "phase 1 CMD parsed");
    } catch (const std::exception& ex) {
        Check(&t, false, std::string("phase 1 parser threw: ") + ex.what());
    }

    // Phase 3: layer create/extract deterministic behavior.
    const auto deltaDir = workRoot / "delta";
    const auto nestedDir = deltaDir / "app";
    std::filesystem::create_directories(nestedDir, ec);
    Check(&t, !ec, "create delta dirs");

    Check(&t, WriteTextFile(deltaDir / "root.txt", "root file\n"), "write root file");
    Check(&t, WriteTextFile(nestedDir / "hello.txt", "hello layer\n"), "write nested file");

    const auto layer1 = store::CreateLayer(stateRoot, deltaDir);
    Check(&t, layer1.ok, "phase 3 create layer first call");
    Check(&t, !layer1.digest.empty(), "phase 3 digest exists");
    Check(&t, layer1.size > 0, "phase 3 size > 0");
    Check(&t, store::LayerExists(stateRoot, layer1.digest), "phase 3 layer exists after create");

    const auto layer2 = store::CreateLayer(stateRoot, deltaDir);
    Check(&t, layer2.ok, "phase 3 create layer second call");
    Check(&t, layer1.digest == layer2.digest, "phase 3 deterministic digest");
    Check(&t, layer1.size == layer2.size, "phase 3 deterministic size");

    const auto extractDir = workRoot / "extract";
    const auto extracted = store::ExtractLayers(stateRoot, {layer1.digest}, extractDir);
    Check(&t, extracted.ok, "phase 3 extract layers");
    Check(&t, ReadTextFile(extractDir / "root.txt") == "root file\n", "phase 3 extracted root content");
    Check(&t, ReadTextFile(extractDir / "app" / "hello.txt") == "hello layer\n",
          "phase 3 extracted nested content");

    // Phase 2: image manifest save/load/list/delete.
    store::ImageManifest manifest;
    manifest.name = "myapp";
    manifest.tag = "latest";
    manifest.created = "2026-04-10T12:20:30Z";
    manifest.config.env = {"FOO=bar"};
    manifest.config.cmd = {"/bin/sh"};
    manifest.config.workingDir = "/app";
    manifest.layers = {{layer1.digest, layer1.size, "COPY . /app"}};

    const auto saved = store::SaveImage(stateRoot, manifest);
    Check(&t, saved.ok, "phase 2 save image");

    const auto loaded = store::LoadImage(stateRoot, "myapp", "latest");
    Check(&t, loaded.ok, "phase 2 load image");
    if (loaded.ok) {
        Check(&t, loaded.manifest.name == "myapp", "phase 2 load name");
        Check(&t, loaded.manifest.tag == "latest", "phase 2 load tag");
        Check(&t, loaded.manifest.created == "2026-04-10T12:20:30Z", "phase 2 created preserved");
        Check(&t, !loaded.manifest.digest.empty(), "phase 2 digest present");
        Check(&t, loaded.manifest.layers.size() == 1U, "phase 2 layer count");
    }

    const auto listed = store::ListImages(stateRoot);
    Check(&t, listed.ok, "phase 2 list images");
    Check(&t, listed.images.size() == 1U, "phase 2 list image count");
    if (!listed.images.empty()) {
        Check(&t, listed.images[0].name == "myapp", "phase 2 list image name");
        Check(&t, listed.images[0].tag == "latest", "phase 2 list image tag");
        Check(&t, listed.images[0].id.size() <= 12U, "phase 2 list image id length");
    }

    // CLI wiring checks for images and rmi.
    Check(&t, engine::Images(stateRoot.string(), {}) == 0, "runtime images command works");
    Check(&t, engine::RMI(stateRoot.string(), {"myapp:latest"}) == 0, "runtime rmi command works");

    const auto missing = store::LoadImage(stateRoot, "myapp", "latest");
    Check(&t, !missing.ok, "phase 2 load deleted image fails");
    Check(&t, !store::LayerExists(stateRoot, layer1.digest), "phase 2 delete image removes layer file");

    // Explicit delete-layer API on a fresh layer.
    const auto layer3 = store::CreateLayer(stateRoot, deltaDir);
    Check(&t, layer3.ok, "phase 3 create layer for delete");
    if (layer3.ok) {
        const auto deleted = store::DeleteLayer(stateRoot, layer3.digest);
        Check(&t, deleted.ok, "phase 3 delete layer api");
    }

    // Build engine + cache (RUN omitted so this works cross-platform).
    const auto buildContext = workRoot / "build-context";
    std::filesystem::create_directories(buildContext, ec);
    Check(&t, !ec, "create build context");
    Check(&t, WriteTextFile(buildContext / "hello.txt", "hello from context\n"), "write build context file");

    const std::string buildFile =
        "FROM scratch\n"
        "WORKDIR /app\n"
        "COPY hello.txt /app/\n"
        "CMD [\"/bin/sh\",\"-c\",\"cat /app/hello.txt\"]\n";
    Check(&t, WriteTextFile(buildContext / "Docksmithfile", buildFile), "write build Docksmithfile");

    const int build1 = engine::Build(stateRoot.string(), {"-t", "demo:latest", buildContext.string()});
    Check(&t, build1 == 0, "phase 4 build first run");

    const auto builtImage1 = store::LoadImage(stateRoot, "demo", "latest");
    Check(&t, builtImage1.ok, "phase 4 image created");
    std::string createdStamp;
    if (builtImage1.ok) {
        createdStamp = builtImage1.manifest.created;
        Check(&t, builtImage1.manifest.layers.size() == 1U, "phase 4 expected one COPY layer");
    }

    const int build2 = engine::Build(stateRoot.string(), {"-t", "demo:latest", buildContext.string()});
    Check(&t, build2 == 0, "phase 4 build second run");

    const auto builtImage2 = store::LoadImage(stateRoot, "demo", "latest");
    Check(&t, builtImage2.ok, "phase 4 image load after rebuild");
    if (builtImage2.ok && !createdStamp.empty()) {
        Check(&t, builtImage2.manifest.created == createdStamp, "phase 2 created timestamp preserved");
    }

    const auto cacheLoaded = store::LoadCacheIndex(stateRoot);
    Check(&t, cacheLoaded.ok, "phase 4 cache index readable");
    if (cacheLoaded.ok) {
        Check(&t, !cacheLoaded.entries.empty(), "phase 4 cache entries exist");
    }

#if defined(__linux__)
    const int runCode = engine::Run(stateRoot.string(), {"demo:latest"});
    Check(&t, runCode == 0, "phase 5 runtime executes image cmd");
#else
    Check(&t, true, "phase 5 runtime skipped on non-linux host");
#endif

    std::filesystem::remove_all(root, ec);

    std::cout << "Passed: " << t.passed << "\n";
    std::cout << "Failed: " << t.failed << "\n";

    if (t.failed != 0) {
        return 1;
    }

    std::cout << "Phase 1 status: working\n";
    std::cout << "Phase 2 status: working\n";
    std::cout << "Phase 3 status: working\n";
    return 0;
}
