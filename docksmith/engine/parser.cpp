#include "engine/parser.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine {
namespace {

constexpr const char* kFrom = "FROM";
constexpr const char* kCopy = "COPY";
constexpr const char* kRun = "RUN";
constexpr const char* kWorkdir = "WORKDIR";
constexpr const char* kEnv = "ENV";
constexpr const char* kCmd = "CMD";

const std::unordered_set<std::string> kSupportedInstructions = {
	kFrom,
	kCopy,
	kRun,
	kWorkdir,
	kEnv,
	kCmd,
};

std::size_t FindFirstNonSpace(const std::string& text) {
	std::size_t index = 0;
	while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
		++index;
	}
	return index;
}

std::vector<std::string> SplitArgs(const std::string& argsText) {
	std::vector<std::string> args;
	std::istringstream iss(argsText);
	std::string token;
	while (iss >> token) {
		args.push_back(token);
	}
	return args;
}

}  // namespace

std::vector<Instruction> ParseDocksmithfile(const std::string& filePath) {
	std::ifstream file(filePath);
	if (!file.is_open()) {
		throw std::runtime_error("Failed to open Docksmithfile: " + filePath);
	}

	std::vector<Instruction> instructions;
	std::string line;
	int lineNumber = 0;

	while (std::getline(file, line)) {
		++lineNumber;

		const std::size_t contentStart = FindFirstNonSpace(line);
		if (contentStart == line.size()) {
			continue;
		}
		if (line[contentStart] == '#') {
			continue;
		}

		std::size_t cursor = contentStart;
		while (cursor < line.size() &&
			   std::isspace(static_cast<unsigned char>(line[cursor])) == 0) {
			++cursor;
		}

		const std::string instructionName = line.substr(contentStart, cursor - contentStart);
		if (kSupportedInstructions.find(instructionName) == kSupportedInstructions.end()) {
			throw std::runtime_error(
				"Unknown instruction " + instructionName + " at line " +
				std::to_string(lineNumber));
		}

		const std::string argsText = cursor < line.size() ? line.substr(cursor) : "";

		Instruction instruction;
		instruction.Type = instructionName;
		instruction.RawText = line;
		instruction.Args = SplitArgs(argsText);
		instruction.LineNumber = lineNumber;
		instructions.push_back(std::move(instruction));
	}

	return instructions;
}

}  // namespace engine
