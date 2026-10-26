// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/relocation.h>
#include <openrayman/sna_file.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openrayman
{
	class game_data;

	// A position inside a loaded block.
	struct memory_location
	{
		const sna_block* block = nullptr;
		std::uint32_t offset = 0;

		std::uint32_t address() const { return block->base + offset; }
	};

	// Rebuilds the game's memory as it was after loading Fix and a level: all their blocks,
	// plus knowledge of which words are pointers and which block each one points into.
	class memory_image
	{
	public:
		// Adds the blocks of a SNA file and the pointers its relocation table describes.
		// Throws format_error if the table refers to blocks or addresses that don't exist.
		void add(sna_file sna, const relocation_table& table);

		// Blocks with data, and data-less blocks for memory the game only allocates at runtime.
		const sna_block* find(block_id id) const;
		std::vector<const sna_block*> blocks() const;

		// True if the word stored at this location is a pointer.
		bool is_pointer(const memory_location& at) const;

		// Follows the pointer stored at a location. Returns nullopt for a null pointer or a word
		// that isn't a pointer, and a location in a data-less block for runtime-only memory.
		std::optional<memory_location> follow(const memory_location& at) const;

		struct statistics
		{
			std::size_t pointers = 0;
			std::size_t into_data = 0;     // targets loaded data
			std::size_t into_runtime = 0;  // targets memory the game only allocates at runtime
			std::size_t dangling = 0;      // target block missing or address outside it
		};
		statistics check() const;

	private:
		static std::uint64_t key(block_id block, std::uint32_t address)
		{
			return std::uint64_t(block.module) << 40 | std::uint64_t(block.id) << 32 | address;
		}

		std::uint32_t read_u32(const memory_location& at) const;

		// Resolves a pointer value written by a block from file `source` into `target`.
		std::optional<memory_location> resolve(std::size_t source, block_id target, std::uint32_t value) const;

		std::vector<std::unique_ptr<sna_block>> m_blocks;
		std::map<block_id, sna_block*> m_by_id;
		// Which file (in order of add()) each block came from.
		std::map<block_id, std::size_t> m_source;
		// How each file sees the memory map: (file, block) -> region.
		std::map<std::pair<std::size_t, block_id>, sna_region> m_views;
		// Pointer slot (block + address) -> block the pointer targets.
		std::unordered_map<std::uint64_t, block_id> m_pointers;
		std::size_t m_files = 0;
	};

	// Loads Fix and one level (by name, as listed in Game.dsb) with all their pointers.
	memory_image load_level_memory(const game_data& game, const std::string& level);
}
