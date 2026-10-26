#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace openrayman
{
	// Reads a whole file into memory. Throws std::runtime_error if it can't be read.
	std::vector<std::byte> read_file(const std::filesystem::path& path);

	// Resolves a relative path below base, matching each component case-insensitively.
	// The game was made for Windows, so its own references ("World\Levels\Fix.sna")
	// rarely match the real casing on disk, which matters on Linux and Android.
	std::optional<std::filesystem::path> find_case_insensitive(
		const std::filesystem::path& base, const std::filesystem::path& relative);
}
