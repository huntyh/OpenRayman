// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/level_header.h>
#include <openrayman/scene.h>

#include <filesystem>
#include <span>

namespace openrayman
{
	struct save_file
	{
		std::vector<std::byte> bytes;
		std::uint8_t checksum = 0; // zero on success; a mismatch throws format_error
		static save_file decode(std::span<const std::byte> encoded);
		explicit save_file(const std::filesystem::path& path);
	private:
		save_file() = default;
	};

	// One registry entry, in file order. Arrays register their elements separately (no header).
	struct save_entry
	{
		std::optional<std::size_t> fix_actor, node;
		memory_location actor; // identifies always actors that have no scene node
		std::uint32_t variable = 0, type = 0, address = 0;
		std::optional<std::uint32_t> element;
		std::optional<std::uint32_t> standard_offset; // standard-game byte, registered by 0x4135E0
		std::uint16_t flags = 0;
		std::size_t offset = 0, size = 0;
		std::optional<unsigned> bit; // packed out-of-world bit, LSB first; size is zero
	};

	struct save_registry
	{
		std::vector<save_entry> general, level;
		std::size_t general_size = 0, level_size = 0;
	};

	// Reconstructs 0x466BD0/0x4135C0's registrations from the loaded declarations and actor tree.
	// Unsupported flagged types throw instead of silently shifting subsequent entries.
	save_registry read_save_registry(const memory_image& memory, const level_header& header, const scene& tree);
	std::filesystem::path save_slot_folder(const std::filesystem::path& saves, unsigned slot);
}
