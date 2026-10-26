// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/sna_file.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <vector>

namespace openrayman
{
	// Which file a relocation table describes. The values are the table type ids used in LEVELS0.DAT.
	enum class relocation_type : std::uint8_t
	{
		sna = 0, // .rtb
		gpt = 1, // .rtp
		snd = 2, // .rts
		ptx = 3, // .rtt
	};

	// One pointer slot: where the pointer is stored, and which block it points into.
	struct relocation
	{
		std::uint32_t address = 0;
		block_id target;
	};

	// Lists every pointer stored in a set of blocks. Pointer slots are identified by their
	// address in the original game's memory.
	struct relocation_table
	{
		std::map<block_id, std::vector<relocation>> blocks;

		std::size_t pointer_count() const;
	};

	// Parses a relocation table file (Fix.rtb, Fix.rtp, ...). Throws format_error.
	relocation_table parse_relocation_table(std::span<const std::byte> data);

	// LEVELS0.DAT: the relocation tables of every level, hidden in 150 MB of filler.
	// See docs/formats/levels0-dat.md.
	class levels_dat
	{
	public:
		// Opens the file and decodes its header. Throws format_error if it doesn't look right.
		explicit levels_dat(const std::filesystem::path& path);

		std::uint32_t level_count() const { return m_level_count; }

		// Reads the table of one type for a level, by its index in the Game.dsb level list.
		relocation_table read(std::uint32_t level, relocation_type type) const;

	private:
		std::uint32_t table_offset(std::uint32_t index, std::uint32_t copy) const;
		relocation_table read_copy(std::uint32_t level, relocation_type type, std::uint32_t copy) const;

		std::filesystem::path m_path;
		std::uint64_t m_file_size = 0;
		std::uint32_t m_sectors = 0;
		std::uint32_t m_level_count = 0;
		std::uint32_t m_copies = 0;
		std::uint32_t m_base = 0;
		std::uint32_t m_sector_size = 0;
	};
}
