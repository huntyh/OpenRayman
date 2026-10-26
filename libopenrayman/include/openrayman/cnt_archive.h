// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openrayman
{
	// One file stored inside a CNT archive.
	struct cnt_entry
	{
		// Full path inside the archive with '/' separators, e.g. "effets_speciaux/Sang.gf".
		std::string path;
		std::uint32_t offset = 0;
		std::uint32_t size = 0;
		std::array<std::uint8_t, 4> xor_key{};
	};

	// CNT archives store the game's GF textures (Textures.cnt, Vignette.cnt).
	// Names are XOR obfuscated with a single byte, file contents with a per file 4 byte key.
	class cnt_archive
	{
	public:
		// Reads the archive directory into memory. File contents are read on demand.
		explicit cnt_archive(const std::filesystem::path& path);

		const std::vector<cnt_entry>& entries() const { return m_entries; }

		// Case-insensitive lookup, accepting either '/' or '\' separators. Returns nullptr if missing.
		const cnt_entry* find(std::string_view path) const;

		// Reads and decodes the contents of one entry.
		std::vector<std::byte> read(const cnt_entry& entry) const;

	private:
		std::filesystem::path m_path;
		std::vector<cnt_entry> m_entries;
		std::unordered_map<std::string, std::size_t> m_index;
	};
}
