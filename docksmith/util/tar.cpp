#include "util/tar.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace util {
namespace {

constexpr std::size_t kTarBlockSize = 512;

struct TarEntry {
	std::filesystem::path sourcePath;
	std::string archivePath;
	bool isDirectory = false;
	bool isSymlink = false;
	std::string symlinkTarget;
	std::uint64_t size = 0;
};

std::size_t CStrNLen(const char* data, const std::size_t maxLen) {
	std::size_t len = 0;
	while (len < maxLen && data[len] != '\0') {
		++len;
	}
	return len;
}

void WriteStringField(char* dest, const std::size_t fieldSize, const std::string& value) {
	std::memset(dest, 0, fieldSize);
	if (value.empty()) {
		return;
	}
	std::memcpy(dest, value.c_str(), std::min(fieldSize, value.size()));
}

void WriteOctalField(char* dest, const std::size_t fieldSize, const std::uint64_t value) {
	std::memset(dest, 0, fieldSize);
	std::string octal;
	if (fieldSize > 1) {
		const auto digits = fieldSize - 1;
		octal.assign(digits, '0');
		std::uint64_t n = value;
		for (std::size_t i = 0; i < digits; ++i) {
			const std::size_t idx = digits - 1 - i;
			octal[idx] = static_cast<char>('0' + (n & 0x7ULL));
			n >>= 3U;
		}
	}
	if (!octal.empty()) {
		std::memcpy(dest, octal.c_str(), octal.size());
	}
}

std::string JoinTarName(const std::array<char, 155>& prefix, const std::array<char, 100>& name) {
	const std::string prefixStr(prefix.data(), CStrNLen(prefix.data(), prefix.size()));
	const std::string nameStr(name.data(), CStrNLen(name.data(), name.size()));
	if (prefixStr.empty()) {
		return nameStr;
	}
	return prefixStr + "/" + nameStr;
}

bool SplitTarPath(const std::string& path, std::string* outPrefix, std::string* outName) {
	if (path.size() <= 100U) {
		*outPrefix = "";
		*outName = path;
		return true;
	}

	const auto slash = path.rfind('/');
	if (slash == std::string::npos || slash == 0) {
		return false;
	}

	const std::string prefix = path.substr(0, slash);
	const std::string name = path.substr(slash + 1U);
	if (prefix.size() > 155U || name.size() > 100U || name.empty()) {
		return false;
	}

	*outPrefix = prefix;
	*outName = name;
	return true;
}

bool IsPathSafe(const std::string& path) {
	if (path.empty()) {
		return false;
	}
	if (path.front() == '/' || path.find("..") != std::string::npos) {
		return false;
	}
	return true;
}

bool ReadFileBytes(const std::filesystem::path& path,
				   std::vector<std::uint8_t>* out,
				   std::string* outError) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		*outError = "failed to open file";
		return false;
	}
	in.seekg(0, std::ios::end);
	const auto size = in.tellg();
	if (size < 0) {
		*outError = "failed to read file size";
		return false;
	}
	out->resize(static_cast<std::size_t>(size));
	in.seekg(0, std::ios::beg);
	if (!out->empty()) {
		in.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(out->size()));
		if (!in) {
			*outError = "failed to read file bytes";
			return false;
		}
	}
	return true;
}

void AppendTarEntryHeader(const TarEntry& entry, std::vector<std::uint8_t>* tar) {
	std::array<char, kTarBlockSize> header{};

	std::string prefix;
	std::string name;
	SplitTarPath(entry.archivePath, &prefix, &name);

	WriteStringField(header.data(), 100, name);
	WriteOctalField(header.data() + 100, 8, entry.isDirectory ? 0755U : (entry.isSymlink ? 0777U : 0644U));
	WriteOctalField(header.data() + 108, 8, 0U);
	WriteOctalField(header.data() + 116, 8, 0U);
	WriteOctalField(header.data() + 124, 12, entry.isDirectory ? 0U : entry.size);
	WriteOctalField(header.data() + 136, 12, 0U);
	std::memset(header.data() + 148, ' ', 8);
	header[156] = entry.isDirectory ? '5' : (entry.isSymlink ? '2' : '0');
	WriteStringField(header.data() + 157, 100, entry.symlinkTarget);
	WriteStringField(header.data() + 257, 6, "ustar");
	WriteStringField(header.data() + 263, 2, "00");
	WriteStringField(header.data() + 265, 32, "");
	WriteStringField(header.data() + 297, 32, "");
	WriteOctalField(header.data() + 329, 8, 0U);
	WriteOctalField(header.data() + 337, 8, 0U);
	WriteStringField(header.data() + 345, 155, prefix);

	unsigned int checksum = 0U;
	for (const auto ch : header) {
		checksum += static_cast<unsigned char>(ch);
	}

	std::array<char, 8> checkField{};
	std::snprintf(checkField.data(), checkField.size(), "%06o", checksum);
	std::memcpy(header.data() + 148, checkField.data(), 6);
	header[154] = '\0';
	header[155] = ' ';

	tar->insert(tar->end(), header.begin(), header.end());
}

bool CollectEntries(const std::filesystem::path& inputDir,
					std::vector<TarEntry>* outEntries,
					std::string* outError) {
	outEntries->clear();

	if (!std::filesystem::exists(inputDir)) {
		*outError = "input directory does not exist";
		return false;
	}
	if (!std::filesystem::is_directory(inputDir)) {
		*outError = "input path is not a directory";
		return false;
	}

	for (std::filesystem::recursive_directory_iterator it(inputDir), end; it != end; ++it) {
		const auto& path = it->path();
		const auto relative = std::filesystem::relative(path, inputDir).generic_string();
		if (relative.empty()) {
			continue;
		}

		TarEntry entry;
		entry.sourcePath = path;
		entry.archivePath = relative;

		const auto status = it->symlink_status();
		if (std::filesystem::is_symlink(status)) {
			entry.isSymlink = true;
			entry.symlinkTarget = std::filesystem::read_symlink(path).generic_string();
		} else if (std::filesystem::is_directory(status)) {
			entry.isDirectory = true;
		} else if (std::filesystem::is_regular_file(status)) {
			entry.size = static_cast<std::uint64_t>(std::filesystem::file_size(path));
		} else {
			continue;
		}

		outEntries->push_back(std::move(entry));
	}

	std::sort(outEntries->begin(), outEntries->end(), [](const TarEntry& a, const TarEntry& b) {
		return a.archivePath < b.archivePath;
	});

	for (const auto& entry : *outEntries) {
		std::string prefix;
		std::string name;
		if (!SplitTarPath(entry.archivePath, &prefix, &name)) {
			*outError = "tar path exceeds ustar limits: " + entry.archivePath;
			return false;
		}
	}

	return true;
}

}  // namespace

bool CreateDeterministicTarFromDirectory(const std::filesystem::path& inputDir,
										 std::vector<std::uint8_t>* outTar,
										 std::string* outError) {
	if (outTar == nullptr || outError == nullptr) {
		return false;
	}

	std::vector<TarEntry> entries;
	if (!CollectEntries(inputDir, &entries, outError)) {
		return false;
	}

	outTar->clear();
	for (const auto& entry : entries) {
		AppendTarEntryHeader(entry, outTar);

		if (!entry.isDirectory && !entry.isSymlink) {
			std::vector<std::uint8_t> fileBytes;
			if (!ReadFileBytes(entry.sourcePath, &fileBytes, outError)) {
				return false;
			}
			outTar->insert(outTar->end(), fileBytes.begin(), fileBytes.end());

			const auto rem = fileBytes.size() % kTarBlockSize;
			if (rem != 0U) {
				outTar->insert(outTar->end(), kTarBlockSize - rem, 0U);
			}
		}
	}

	outTar->insert(outTar->end(), kTarBlockSize * 2U, 0U);
	outError->clear();
	return true;
}

bool ExtractTarBytes(const std::vector<std::uint8_t>& tarBytes,
					 const std::filesystem::path& targetDir,
					 std::string* outError) {
	if (outError == nullptr) {
		return false;
	}

	try {
		std::filesystem::create_directories(targetDir);
	} catch (const std::exception& ex) {
		*outError = ex.what();
		return false;
	}

	std::size_t offset = 0;
	while (offset + kTarBlockSize <= tarBytes.size()) {
		const std::uint8_t* header = tarBytes.data() + offset;
		bool allZero = true;
		for (std::size_t i = 0; i < kTarBlockSize; ++i) {
			if (header[i] != 0U) {
				allZero = false;
				break;
			}
		}
		if (allZero) {
			break;
		}

		std::array<char, 100> nameField{};
		std::array<char, 155> prefixField{};
		std::memcpy(nameField.data(), header, nameField.size());
		std::memcpy(prefixField.data(), header + 345, prefixField.size());
		const std::string archivePath = JoinTarName(prefixField, nameField);
		if (!IsPathSafe(archivePath)) {
			*outError = "unsafe tar path";
			return false;
		}

		std::array<char, 12> sizeField{};
		std::memcpy(sizeField.data(), header + 124, sizeField.size());
		const std::uint64_t size = std::strtoull(sizeField.data(), nullptr, 8);
		const char typeFlag = static_cast<char>(header[156]);

		const auto outPath = targetDir / std::filesystem::path(archivePath);

		try {
			if (typeFlag == '5') {
				if (std::filesystem::exists(outPath) && !std::filesystem::is_directory(outPath)) {
					std::filesystem::remove_all(outPath);
				}
				std::filesystem::create_directories(outPath);
			} else if (typeFlag == '2') {
				if (std::filesystem::exists(outPath)) {
					std::filesystem::remove_all(outPath);
				}
				std::filesystem::create_directories(outPath.parent_path());

				std::array<char, 100> linkField{};
				std::memcpy(linkField.data(), header + 157, linkField.size());
				const std::string linkTarget(linkField.data(), CStrNLen(linkField.data(), linkField.size()));
				std::error_code ec;
				std::filesystem::create_symlink(std::filesystem::path(linkTarget), outPath, ec);
				if (ec) {
					*outError = ec.message();
					return false;
				}
			} else {
				if (std::filesystem::exists(outPath) && std::filesystem::is_directory(outPath)) {
					std::filesystem::remove_all(outPath);
				}
				std::filesystem::create_directories(outPath.parent_path());
				std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
				if (!out) {
					*outError = "failed to create output file";
					return false;
				}

				const std::size_t dataOffset = offset + kTarBlockSize;
				if (dataOffset + size > tarBytes.size()) {
					*outError = "tar payload truncated";
					return false;
				}
				out.write(reinterpret_cast<const char*>(tarBytes.data() + dataOffset),
						  static_cast<std::streamsize>(size));
				if (!out) {
					*outError = "failed to write output file";
					return false;
				}
			}
		} catch (const std::exception& ex) {
			*outError = ex.what();
			return false;
		}

		const std::size_t payload = static_cast<std::size_t>(size);
		const std::size_t padded = ((payload + kTarBlockSize - 1U) / kTarBlockSize) * kTarBlockSize;
		offset += kTarBlockSize + padded;
	}

	outError->clear();
	return true;
}

bool ExtractTarFile(const std::filesystem::path& tarFile,
					const std::filesystem::path& targetDir,
					std::string* outError) {
	if (outError == nullptr) {
		return false;
	}

	std::vector<std::uint8_t> tarBytes;
	if (!ReadFileBytes(tarFile, &tarBytes, outError)) {
		return false;
	}

	return ExtractTarBytes(tarBytes, targetDir, outError);
}

}  // namespace util
