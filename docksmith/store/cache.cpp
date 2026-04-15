#include "store/cache.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace store {
namespace {

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

std::string BuildCacheJson(const std::unordered_map<std::string, std::string>& entries) {
	std::vector<std::pair<std::string, std::string>> sorted(entries.begin(), entries.end());
	std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

	std::string json = "{\"entries\":[";
	for (std::size_t i = 0; i < sorted.size(); ++i) {
		if (i != 0U) {
			json += ",";
		}
		json += "{\"key\":\"" + EscapeJson(sorted[i].first) + "\",\"layerDigest\":\"" +
				EscapeJson(sorted[i].second) + "\"}";
	}
	json += "]}";
	return json;
}

bool ParseQuoted(const std::string& text,
				 std::size_t* index,
				 std::string* out,
				 std::string* outError) {
	if (*index >= text.size() || text[*index] != '"') {
		*outError = "Invalid cache format";
		return false;
	}
	++(*index);

	std::string value;
	while (*index < text.size()) {
		const char ch = text[*index];
		++(*index);
		if (ch == '"') {
			*out = value;
			return true;
		}
		if (ch != '\\') {
			value.push_back(ch);
			continue;
		}
		if (*index >= text.size()) {
			*outError = "Invalid cache format";
			return false;
		}
		const char esc = text[*index];
		++(*index);
		switch (esc) {
			case '"':
			case '\\':
			case '/':
				value.push_back(esc);
				break;
			case 'n':
				value.push_back('\n');
				break;
			case 'r':
				value.push_back('\r');
				break;
			case 't':
				value.push_back('\t');
				break;
			default:
				*outError = "Invalid cache format";
				return false;
		}
	}

	*outError = "Invalid cache format";
	return false;
}

void SkipWhitespace(const std::string& text, std::size_t* index) {
	while (*index < text.size() && std::isspace(static_cast<unsigned char>(text[*index])) != 0) {
		++(*index);
	}
}

bool Consume(const std::string& text, std::size_t* index, const char expected, std::string* outError) {
	SkipWhitespace(text, index);
	if (*index >= text.size() || text[*index] != expected) {
		*outError = "Invalid cache format";
		return false;
	}
	++(*index);
	return true;
}

bool ParseCacheJson(const std::string& text,
					std::unordered_map<std::string, std::string>* out,
					std::string* outError) {
	std::size_t i = 0;
	out->clear();

	if (!Consume(text, &i, '{', outError)) {
		return false;
	}
	SkipWhitespace(text, &i);

	std::string entriesKey;
	if (!ParseQuoted(text, &i, &entriesKey, outError) || entriesKey != "entries") {
		*outError = "Invalid cache format";
		return false;
	}
	if (!Consume(text, &i, ':', outError)) {
		return false;
	}
	if (!Consume(text, &i, '[', outError)) {
		return false;
	}

	SkipWhitespace(text, &i);
	if (i < text.size() && text[i] == ']') {
		++i;
	} else {
		while (true) {
			if (!Consume(text, &i, '{', outError)) {
				return false;
			}

			std::string keyName;
			if (!ParseQuoted(text, &i, &keyName, outError) || keyName != "key") {
				*outError = "Invalid cache format";
				return false;
			}
			if (!Consume(text, &i, ':', outError)) {
				return false;
			}
			std::string keyValue;
			if (!ParseQuoted(text, &i, &keyValue, outError)) {
				return false;
			}
			if (!Consume(text, &i, ',', outError)) {
				return false;
			}

			std::string digestName;
			if (!ParseQuoted(text, &i, &digestName, outError) || digestName != "layerDigest") {
				*outError = "Invalid cache format";
				return false;
			}
			if (!Consume(text, &i, ':', outError)) {
				return false;
			}
			std::string digestValue;
			if (!ParseQuoted(text, &i, &digestValue, outError)) {
				return false;
			}
			if (!Consume(text, &i, '}', outError)) {
				return false;
			}

			(*out)[keyValue] = digestValue;

			SkipWhitespace(text, &i);
			if (i >= text.size()) {
				*outError = "Invalid cache format";
				return false;
			}
			if (text[i] == ']') {
				++i;
				break;
			}
			if (text[i] != ',') {
				*outError = "Invalid cache format";
				return false;
			}
			++i;
		}
	}

	if (!Consume(text, &i, '}', outError)) {
		return false;
	}
	SkipWhitespace(text, &i);
	if (i != text.size()) {
		*outError = "Invalid cache format";
		return false;
	}
	return true;
}

std::filesystem::path CacheIndexPath(const std::filesystem::path& stateRoot) {
	return stateRoot / "cache" / "index.json";
}

}  // namespace

CacheLoadResult LoadCacheIndex(const std::filesystem::path& stateRoot) {
	const auto path = CacheIndexPath(stateRoot);
	if (!std::filesystem::exists(path)) {
		return {true, {}, ""};
	}

	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return {false, {}, "Failed to read cache index"};
	}

	std::stringstream ss;
	ss << in.rdbuf();
	if (!in && !in.eof()) {
		return {false, {}, "Failed to read cache index"};
	}

	std::unordered_map<std::string, std::string> entries;
	std::string parseError;
	if (!ParseCacheJson(ss.str(), &entries, &parseError)) {
		return {false, {}, parseError};
	}

	return {true, std::move(entries), ""};
}

CacheResult SaveCacheIndex(const std::filesystem::path& stateRoot,
						   const std::unordered_map<std::string, std::string>& entries) {
	const auto cacheDir = stateRoot / "cache";
	const auto path = CacheIndexPath(stateRoot);

	try {
		std::filesystem::create_directories(cacheDir);
	} catch (const std::exception& ex) {
		return {false, ex.what()};
	}

	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		return {false, "Failed to write cache index"};
	}

	out << BuildCacheJson(entries);
	if (!out) {
		return {false, "Failed to write cache index"};
	}

	return {true, ""};
}

}  // namespace store
