#include "store/images.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "store/layers.hpp"
#include "util/hash.hpp"

namespace store {
namespace {

enum class JsonType {
	kNull,
	kBool,
	kNumber,
	kString,
	kArray,
	kObject,
};

struct JsonValue {
	JsonType type = JsonType::kNull;
	bool boolValue = false;
	std::int64_t numberValue = 0;
	std::string stringValue;
	std::vector<JsonValue> arrayValue;
	std::map<std::string, JsonValue> objectValue;
};

class JsonParser {
  public:
	explicit JsonParser(std::string text) : text_(std::move(text)) {}

	bool Parse(JsonValue* out, std::string* outError) {
		SkipWhitespace();
		if (!ParseValue(out, outError)) {
			return false;
		}
		SkipWhitespace();
		if (!IsEnd()) {
			*outError = "trailing content";
			return false;
		}
		return true;
	}

  private:
	bool ParseValue(JsonValue* out, std::string* outError) {
		if (IsEnd()) {
			*outError = "unexpected end";
			return false;
		}
		const char ch = text_[pos_];
		if (ch == '{') {
			return ParseObject(out, outError);
		}
		if (ch == '[') {
			return ParseArray(out, outError);
		}
		if (ch == '"') {
			out->type = JsonType::kString;
			return ParseString(&out->stringValue, outError);
		}
		if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '-') {
			out->type = JsonType::kNumber;
			return ParseNumber(&out->numberValue, outError);
		}
		if (MatchLiteral("true")) {
			out->type = JsonType::kBool;
			out->boolValue = true;
			return true;
		}
		if (MatchLiteral("false")) {
			out->type = JsonType::kBool;
			out->boolValue = false;
			return true;
		}
		if (MatchLiteral("null")) {
			out->type = JsonType::kNull;
			return true;
		}

		*outError = "unexpected token";
		return false;
	}

	bool ParseObject(JsonValue* out, std::string* outError) {
		out->type = JsonType::kObject;
		out->objectValue.clear();
		++pos_;
		SkipWhitespace();

		if (!IsEnd() && text_[pos_] == '}') {
			++pos_;
			return true;
		}

		while (true) {
			SkipWhitespace();
			std::string key;
			if (!ParseString(&key, outError)) {
				return false;
			}
			SkipWhitespace();
			if (IsEnd() || text_[pos_] != ':') {
				*outError = "expected ':'";
				return false;
			}
			++pos_;
			SkipWhitespace();

			JsonValue value;
			if (!ParseValue(&value, outError)) {
				return false;
			}
			out->objectValue.emplace(std::move(key), std::move(value));

			SkipWhitespace();
			if (IsEnd()) {
				*outError = "unexpected end";
				return false;
			}
			if (text_[pos_] == '}') {
				++pos_;
				break;
			}
			if (text_[pos_] != ',') {
				*outError = "expected ','";
				return false;
			}
			++pos_;
		}
		return true;
	}

	bool ParseArray(JsonValue* out, std::string* outError) {
		out->type = JsonType::kArray;
		out->arrayValue.clear();
		++pos_;
		SkipWhitespace();

		if (!IsEnd() && text_[pos_] == ']') {
			++pos_;
			return true;
		}

		while (true) {
			JsonValue value;
			if (!ParseValue(&value, outError)) {
				return false;
			}
			out->arrayValue.push_back(std::move(value));

			SkipWhitespace();
			if (IsEnd()) {
				*outError = "unexpected end";
				return false;
			}
			if (text_[pos_] == ']') {
				++pos_;
				break;
			}
			if (text_[pos_] != ',') {
				*outError = "expected ','";
				return false;
			}
			++pos_;
			SkipWhitespace();
		}
		return true;
	}

	bool ParseString(std::string* out, std::string* outError) {
		if (IsEnd() || text_[pos_] != '"') {
			*outError = "expected string";
			return false;
		}
		++pos_;

		std::string result;
		while (!IsEnd()) {
			const char ch = text_[pos_++];
			if (ch == '"') {
				*out = result;
				return true;
			}
			if (ch != '\\') {
				result.push_back(ch);
				continue;
			}
			if (IsEnd()) {
				*outError = "invalid escape";
				return false;
			}
			const char esc = text_[pos_++];
			switch (esc) {
				case '"':
				case '\\':
				case '/':
					result.push_back(esc);
					break;
				case 'b':
					result.push_back('\b');
					break;
				case 'f':
					result.push_back('\f');
					break;
				case 'n':
					result.push_back('\n');
					break;
				case 'r':
					result.push_back('\r');
					break;
				case 't':
					result.push_back('\t');
					break;
				default:
					*outError = "unsupported escape";
					return false;
			}
		}

		*outError = "unterminated string";
		return false;
	}

	bool ParseNumber(std::int64_t* out, std::string* outError) {
		const auto start = pos_;
		if (text_[pos_] == '-') {
			++pos_;
		}
		if (IsEnd() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
			*outError = "invalid number";
			return false;
		}
		while (!IsEnd() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
			++pos_;
		}

		try {
			*out = std::stoll(text_.substr(start, pos_ - start));
		} catch (const std::exception&) {
			*outError = "invalid number";
			return false;
		}
		return true;
	}

	bool MatchLiteral(const std::string& value) {
		if (text_.compare(pos_, value.size(), value) != 0) {
			return false;
		}
		pos_ += value.size();
		return true;
	}

	void SkipWhitespace() {
		while (!IsEnd() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
			++pos_;
		}
	}

	bool IsEnd() const {
		return pos_ >= text_.size();
	}

	std::string text_;
	std::size_t pos_ = 0;
};

std::string EscapeJson(const std::string& text) {
	std::string out;
	out.reserve(text.size() + 8);
	for (const char ch : text) {
		switch (ch) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\b':
				out += "\\b";
				break;
			case '\f':
				out += "\\f";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				out.push_back(ch);
				break;
		}
	}
	return out;
}

void AppendString(std::string* out, const std::string& value) {
	*out += '"';
	*out += EscapeJson(value);
	*out += '"';
}

std::string SerializeManifest(const ImageManifest& manifest, bool digestBlank) {
	std::string out;
	out.reserve(512);

	out += "{";

	out += "\"name\":";
	AppendString(&out, manifest.name);
	out += ",\"tag\":";
	AppendString(&out, manifest.tag);
	out += ",\"digest\":";
	AppendString(&out, digestBlank ? "" : manifest.digest);
	out += ",\"created\":";
	AppendString(&out, manifest.created);

	out += ",\"config\":{";
	out += "\"Env\":[";
	for (std::size_t i = 0; i < manifest.config.env.size(); ++i) {
		if (i != 0U) {
			out += ",";
		}
		AppendString(&out, manifest.config.env[i]);
	}
	out += "],\"Cmd\":[";
	for (std::size_t i = 0; i < manifest.config.cmd.size(); ++i) {
		if (i != 0U) {
			out += ",";
		}
		AppendString(&out, manifest.config.cmd[i]);
	}
	out += "],\"WorkingDir\":";
	AppendString(&out, manifest.config.workingDir);
	out += "}";

	out += ",\"layers\":[";
	for (std::size_t i = 0; i < manifest.layers.size(); ++i) {
		if (i != 0U) {
			out += ",";
		}
		const auto& layer = manifest.layers[i];
		out += "{";
		out += "\"digest\":";
		AppendString(&out, layer.digest);
		out += ",\"size\":" + std::to_string(layer.size);
		out += ",\"createdBy\":";
		AppendString(&out, layer.createdBy);
		out += "}";
	}
	out += "]";

	out += "}";
	return out;
}

std::string CurrentIso8601Utc() {
	const auto now = std::chrono::system_clock::now();
	const auto t = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
#if defined(_WIN32)
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif

	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
	return oss.str();
}

std::filesystem::path ImagesDir(const std::filesystem::path& stateRoot) {
	return stateRoot / "images";
}

std::filesystem::path ImagePath(const std::filesystem::path& stateRoot,
								const std::string& name,
								const std::string& tag) {
	return ImagesDir(stateRoot) / (name + "_" + tag + ".json");
}

bool ExtractStringField(const std::map<std::string, JsonValue>& obj,
						const std::string& key,
						std::string* out) {
	const auto it = obj.find(key);
	if (it == obj.end() || it->second.type != JsonType::kString) {
		return false;
	}
	*out = it->second.stringValue;
	return true;
}

bool ExtractStringArrayField(const std::map<std::string, JsonValue>& obj,
							 const std::string& key,
							 std::vector<std::string>* out) {
	const auto it = obj.find(key);
	if (it == obj.end() || it->second.type != JsonType::kArray) {
		return false;
	}
	out->clear();
	for (const auto& v : it->second.arrayValue) {
		if (v.type != JsonType::kString) {
			return false;
		}
		out->push_back(v.stringValue);
	}
	return true;
}

bool ParseManifestJson(const std::string& jsonText, ImageManifest* outManifest) {
	JsonParser parser(jsonText);
	JsonValue root;
	std::string parseError;
	if (!parser.Parse(&root, &parseError)) {
		return false;
	}
	if (root.type != JsonType::kObject) {
		return false;
	}

	ImageManifest manifest;
	if (!ExtractStringField(root.objectValue, "name", &manifest.name) ||
		!ExtractStringField(root.objectValue, "tag", &manifest.tag) ||
		!ExtractStringField(root.objectValue, "digest", &manifest.digest) ||
		!ExtractStringField(root.objectValue, "created", &manifest.created)) {
		return false;
	}

	const auto configIt = root.objectValue.find("config");
	if (configIt == root.objectValue.end() || configIt->second.type != JsonType::kObject) {
		return false;
	}
	if (!ExtractStringArrayField(configIt->second.objectValue, "Env", &manifest.config.env) ||
		!ExtractStringArrayField(configIt->second.objectValue, "Cmd", &manifest.config.cmd) ||
		!ExtractStringField(configIt->second.objectValue, "WorkingDir", &manifest.config.workingDir)) {
		return false;
	}

	const auto layersIt = root.objectValue.find("layers");
	if (layersIt == root.objectValue.end() || layersIt->second.type != JsonType::kArray) {
		return false;
	}

	manifest.layers.clear();
	for (const auto& layerValue : layersIt->second.arrayValue) {
		if (layerValue.type != JsonType::kObject) {
			return false;
		}
		Layer layer;
		if (!ExtractStringField(layerValue.objectValue, "digest", &layer.digest) ||
			!ExtractStringField(layerValue.objectValue, "createdBy", &layer.createdBy)) {
			return false;
		}

		const auto sizeIt = layerValue.objectValue.find("size");
		if (sizeIt == layerValue.objectValue.end() || sizeIt->second.type != JsonType::kNumber) {
			return false;
		}
		layer.size = sizeIt->second.numberValue;

		manifest.layers.push_back(std::move(layer));
	}

	*outManifest = std::move(manifest);
	return true;
}

std::string ShortDigestId(const std::string& digest) {
	std::string value = digest;
	constexpr const char* kPrefix = "sha256:";
	if (value.rfind(kPrefix, 0) == 0U) {
		value = value.substr(7);
	}
	if (value.size() > 12U) {
		return value.substr(0, 12U);
	}
	return value;
}

}  // namespace

StoreResult SaveImage(const std::filesystem::path& stateRoot, ImageManifest manifest) {
	if (manifest.created.empty()) {
		manifest.created = CurrentIso8601Utc();
	}

	const std::string digestSource = SerializeManifest(manifest, true);
	manifest.digest = "sha256:" + util::HashString(digestSource);
	const std::string finalJson = SerializeManifest(manifest, false);

	const auto imagesDir = ImagesDir(stateRoot);
	const auto manifestPath = ImagePath(stateRoot, manifest.name, manifest.tag);

	try {
		std::filesystem::create_directories(imagesDir);
	} catch (const std::exception& ex) {
		return {false, ex.what()};
	}

	std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
	if (!out) {
		return {false, "Failed to write manifest"};
	}
	out << finalJson;
	if (!out) {
		return {false, "Failed to write manifest"};
	}

	return {true, ""};
}

LoadImageResult LoadImage(const std::filesystem::path& stateRoot,
						  const std::string& name,
						  const std::string& tag) {
	const auto manifestPath = ImagePath(stateRoot, name, tag);
	if (!std::filesystem::exists(manifestPath)) {
		return {false, {}, "Image " + name + ":" + tag + " not found"};
	}

	std::ifstream in(manifestPath, std::ios::binary);
	if (!in) {
		return {false, {}, "Failed to read manifest"};
	}

	std::stringstream ss;
	ss << in.rdbuf();
	if (!in && !in.eof()) {
		return {false, {}, "Failed to read manifest"};
	}

	ImageManifest manifest;
	if (!ParseManifestJson(ss.str(), &manifest)) {
		return {false, {}, "Invalid manifest format"};
	}

	return {true, std::move(manifest), ""};
}

ListImagesResult ListImages(const std::filesystem::path& stateRoot) {
	const auto imagesDir = ImagesDir(stateRoot);
	if (!std::filesystem::exists(imagesDir)) {
		return {true, {}, ""};
	}

	ListImagesResult result;
	result.ok = true;

	try {
		for (const auto& entry : std::filesystem::directory_iterator(imagesDir)) {
			if (!entry.is_regular_file()) {
				continue;
			}
			if (entry.path().extension() != ".json") {
				continue;
			}

			std::ifstream in(entry.path(), std::ios::binary);
			if (!in) {
				return {false, {}, "Failed to read manifest"};
			}
			std::stringstream ss;
			ss << in.rdbuf();
			if (!in && !in.eof()) {
				return {false, {}, "Failed to read manifest"};
			}

			ImageManifest manifest;
			if (!ParseManifestJson(ss.str(), &manifest)) {
				return {false, {}, "Invalid manifest format"};
			}

			result.images.push_back({manifest.name, manifest.tag, ShortDigestId(manifest.digest),
									 manifest.created});
		}
	} catch (const std::exception& ex) {
		return {false, {}, ex.what()};
	}

	std::sort(result.images.begin(), result.images.end(), [](const ImageSummary& a, const ImageSummary& b) {
		if (a.name == b.name) {
			return a.tag < b.tag;
		}
		return a.name < b.name;
	});

	return result;
}

StoreResult DeleteImage(const std::filesystem::path& stateRoot,
						const std::string& name,
						const std::string& tag) {
	const auto loaded = LoadImage(stateRoot, name, tag);
	if (!loaded.ok) {
		return {false, loaded.error};
	}

	const auto manifestPath = ImagePath(stateRoot, name, tag);
	try {
		if (!std::filesystem::remove(manifestPath)) {
			return {false, "Failed to delete manifest"};
		}
	} catch (const std::exception& ex) {
		return {false, ex.what()};
	}

	for (const auto& layer : loaded.manifest.layers) {
		const auto deleted = DeleteLayer(stateRoot, layer.digest);
		if (!deleted.ok) {
			return deleted;
		}
	}

	return {true, ""};
}

}  // namespace store
