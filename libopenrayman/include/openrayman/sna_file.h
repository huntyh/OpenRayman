#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace openrayman
{
	// Identifies a memory block: the engine module that owns it and a per-module id.
	// Fix uses id 0 or 1 for its blocks, levels use 1 or 2, so ids never clash between the two.
	struct block_id
	{
		std::uint8_t module = 0;
		std::uint8_t id = 0;

		auto operator<=>(const block_id&) const = default;
		std::string to_string() const;
	};

	// One block of a SNA file: a chunk of the original game's memory, saved with the
	// address it lived at. Pointers inside are absolute addresses into such blocks.
	struct sna_block
	{
		block_id id;
		std::uint8_t unknown = 0;

		// Address of the first byte in the original game's memory.
		std::uint32_t base = 0;
		// End of the memory the game reserved for this block. Can be larger than the data,
		// and blocks without data are runtime-only regions that pointers may still target.
		std::uint32_t reserved_end = 0;

		std::vector<std::byte> data;

		bool contains(std::uint32_t address) const { return address >= base && address < base + data.size(); }
		bool reserves(std::uint32_t address) const { return address >= base && address <= reserved_end; }
	};

	// Where a file believes a block lives. A level announces the whole memory map, including
	// Fix's blocks, and its pointers into Fix use these addresses, which can differ from the
	// ones in Fix.sna itself (e.g. by 8 bytes).
	struct sna_region
	{
		block_id id;
		std::uint32_t base = 0;
		std::uint32_t reserved_end = 0;
	};

	// A SNA file: the memory blocks a level (or Fix, the data shared by all levels) loads.
	class sna_file
	{
	public:
		// Parses an SNA file, including the XOR mask covering all of it. Throws format_error.
		explicit sna_file(std::span<const std::byte> raw);

		// Blocks that carry data in this file.
		const std::vector<sna_block>& blocks() const { return m_blocks; }

		// Every block this file announces, with or without data, as this file sees it.
		const std::vector<sna_region>& regions() const { return m_regions; }

		// The data block with this id, or nullptr.
		const sna_block* find(block_id id) const;

	private:
		std::vector<sna_block> m_blocks;
		std::vector<sna_region> m_regions;
	};
}
