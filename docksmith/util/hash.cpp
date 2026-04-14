#include "util/hash.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace util {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
	0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
	0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
	0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
	0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
	0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
	0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
	0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
	0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
	0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
	0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
	0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

inline std::uint32_t RotateRight(const std::uint32_t x, const std::uint32_t n) {
	return (x >> n) | (x << (32U - n));
}

std::string ToHex(const std::array<std::uint8_t, 32>& digest) {
	std::ostringstream oss;
	oss << std::hex << std::setfill('0');
	for (const auto byte : digest) {
		oss << std::setw(2) << static_cast<unsigned int>(byte);
	}
	return oss.str();
}

std::array<std::uint8_t, 32> Sha256(const std::vector<std::uint8_t>& bytes) {
	std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8ULL;

	std::vector<std::uint8_t> msg = bytes;
	msg.push_back(0x80U);
	while ((msg.size() % 64U) != 56U) {
		msg.push_back(0x00U);
	}
	for (int shift = 56; shift >= 0; shift -= 8) {
		msg.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffULL));
	}

	std::array<std::uint32_t, 8> state = {
		0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
		0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
	};

	std::array<std::uint32_t, 64> w{};

	for (std::size_t offset = 0; offset < msg.size(); offset += 64U) {
		for (std::size_t i = 0; i < 16U; ++i) {
			const auto base = offset + (i * 4U);
			w[i] = (static_cast<std::uint32_t>(msg[base]) << 24U) |
				   (static_cast<std::uint32_t>(msg[base + 1U]) << 16U) |
				   (static_cast<std::uint32_t>(msg[base + 2U]) << 8U) |
				   static_cast<std::uint32_t>(msg[base + 3U]);
		}

		for (std::size_t i = 16U; i < 64U; ++i) {
			const std::uint32_t s0 = RotateRight(w[i - 15U], 7U) ^ RotateRight(w[i - 15U], 18U) ^
									 (w[i - 15U] >> 3U);
			const std::uint32_t s1 = RotateRight(w[i - 2U], 17U) ^ RotateRight(w[i - 2U], 19U) ^
									 (w[i - 2U] >> 10U);
			w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
		}

		std::uint32_t a = state[0];
		std::uint32_t b = state[1];
		std::uint32_t c = state[2];
		std::uint32_t d = state[3];
		std::uint32_t e = state[4];
		std::uint32_t f = state[5];
		std::uint32_t g = state[6];
		std::uint32_t h = state[7];

		for (std::size_t i = 0; i < 64U; ++i) {
			const std::uint32_t S1 = RotateRight(e, 6U) ^ RotateRight(e, 11U) ^ RotateRight(e, 25U);
			const std::uint32_t ch = (e & f) ^ ((~e) & g);
			const std::uint32_t temp1 = h + S1 + ch + kSha256K[i] + w[i];
			const std::uint32_t S0 = RotateRight(a, 2U) ^ RotateRight(a, 13U) ^ RotateRight(a, 22U);
			const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			const std::uint32_t temp2 = S0 + maj;

			h = g;
			g = f;
			f = e;
			e = d + temp1;
			d = c;
			c = b;
			b = a;
			a = temp1 + temp2;
		}

		state[0] += a;
		state[1] += b;
		state[2] += c;
		state[3] += d;
		state[4] += e;
		state[5] += f;
		state[6] += g;
		state[7] += h;
	}

	std::array<std::uint8_t, 32> digest{};
	for (std::size_t i = 0; i < state.size(); ++i) {
		digest[i * 4U] = static_cast<std::uint8_t>((state[i] >> 24U) & 0xffU);
		digest[(i * 4U) + 1U] = static_cast<std::uint8_t>((state[i] >> 16U) & 0xffU);
		digest[(i * 4U) + 2U] = static_cast<std::uint8_t>((state[i] >> 8U) & 0xffU);
		digest[(i * 4U) + 3U] = static_cast<std::uint8_t>(state[i] & 0xffU);
	}

	return digest;
}

}  // namespace

std::string HashBytes(const std::vector<std::uint8_t>& bytes) {
	return ToHex(Sha256(bytes));
}

std::string HashString(const std::string& text) {
	return HashBytes(std::vector<std::uint8_t>(text.begin(), text.end()));
}

bool HashFile(const std::filesystem::path& path, std::string* outHash, std::string* outError) {
	if (outHash == nullptr || outError == nullptr) {
		return false;
	}

	std::ifstream in(path, std::ios::binary);
	if (!in) {
		*outError = "failed to open file";
		return false;
	}

	std::vector<std::uint8_t> bytes;
	in.seekg(0, std::ios::end);
	const auto size = in.tellg();
	if (size < 0) {
		*outError = "failed to read file size";
		return false;
	}

	bytes.resize(static_cast<std::size_t>(size));
	in.seekg(0, std::ios::beg);
	if (!bytes.empty()) {
		in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!in) {
			*outError = "failed to read file";
			return false;
		}
	}

	*outHash = HashBytes(bytes);
	outError->clear();
	return true;
}

}  // namespace util
