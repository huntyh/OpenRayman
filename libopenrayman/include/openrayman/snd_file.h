// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace openrayman
{
	// One pair of `<Level>.snd` or `Fix.snd`: a sound event and the slot that names it. Scripts
	// (node type 30) and animations (event descriptors) hold slots, not events; the loader writes the
	// event into each slot (Rayman2.exe 0x44D2F0). See docs/formats/sound.md.
	struct sound_slot
	{
		std::uint32_t event = 0; // (bank << 16) | event index
		std::uint32_t slot = 0;  // an address in the level's (or Fix's) memory
	};

	// Reads a .snd file's pairs (8 bytes each). Throws format_error if its size isn't a multiple of 8.
	std::vector<sound_slot> read_snd(std::span<const std::byte> data);
	std::vector<sound_slot> read_snd(const std::filesystem::path& path);
}
