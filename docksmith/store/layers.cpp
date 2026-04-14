#include "store/layers.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "util/hash.hpp"
#include "util/tar.hpp"

namespace store {
namespace {

std::filesystem::path LayersDir(const std::filesystem::path& stateRoot) {
	return stateRoot / "layers";
}

std::filesystem::path LayerPath(const std::filesystem::path& stateRoot, const std::string& digest) {
	return LayersDir(stateRoot) / (digest + ".tar");
}

}  // namespace

CreateLayerResult CreateLayer(const std::filesystem::path& stateRoot,
							  const std::filesystem::path& deltaDir) {
	std::vector<std::uint8_t> tarBytes;
	std::string tarError;
	if (!util::CreateDeterministicTarFromDirectory(deltaDir, &tarBytes, &tarError)) {
		return {false, "", 0, tarError};
	}

	const std::string hash = util::HashBytes(tarBytes);
	const std::string digest = "sha256:" + hash;

	const auto layersDir = LayersDir(stateRoot);
	const auto outPath = LayerPath(stateRoot, digest);

	try {
		std::filesystem::create_directories(layersDir);
	} catch (const std::exception& ex) {
		return {false, "", 0, ex.what()};
	}

	if (!std::filesystem::exists(outPath)) {
		std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
		if (!out) {
			return {false, "", 0, "failed to create layer file"};
		}
		if (!tarBytes.empty()) {
			out.write(reinterpret_cast<const char*>(tarBytes.data()),
					  static_cast<std::streamsize>(tarBytes.size()));
			if (!out) {
				return {false, "", 0, "failed to write layer file"};
			}
		}
	}

	return {true, digest, static_cast<std::int64_t>(tarBytes.size()), ""};
}

StoreResult ExtractLayers(const std::filesystem::path& stateRoot,
						  const std::vector<std::string>& layerList,
						  const std::filesystem::path& targetDir) {
	for (const auto& digest : layerList) {
		const auto tarPath = LayerPath(stateRoot, digest);
		if (!std::filesystem::exists(tarPath)) {
			return {false, "Layer " + digest + " not found"};
		}

		std::string extractError;
		if (!util::ExtractTarFile(tarPath, targetDir, &extractError)) {
			return {false, extractError};
		}
	}

	return {true, ""};
}

bool LayerExists(const std::filesystem::path& stateRoot, const std::string& digest) {
	return std::filesystem::exists(LayerPath(stateRoot, digest));
}

StoreResult DeleteLayer(const std::filesystem::path& stateRoot, const std::string& digest) {
	const auto path = LayerPath(stateRoot, digest);
	if (!std::filesystem::exists(path)) {
		return {false, "Layer " + digest + " not found"};
	}

	try {
		if (!std::filesystem::remove(path)) {
			return {false, "Failed to delete layer file"};
		}
	} catch (const std::exception& ex) {
		return {false, ex.what()};
	}

	return {true, ""};
}

}  // namespace store
