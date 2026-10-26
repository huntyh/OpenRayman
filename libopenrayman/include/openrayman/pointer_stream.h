// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/binary_reader.h>
#include <openrayman/memory_image.h>
#include <openrayman/relocation.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openrayman
{
	// Reads a GPT or PTX file. Unlike SNA files these aren't memory dumps but streams the game
	// reads field by field. Their relocation table (.rtp, .rtt) lists the value of every non-null
	// pointer in the order they appear, with the block each one points into.
	// See docs/formats/gpt.md.
	class pointer_stream : public binary_reader
	{
	public:
		// `file` picks whose view of the memory map the pointers use, as an index in the order
		// files were added to the memory image (0 is Fix, 1 is the level).
		pointer_stream(std::span<const std::byte> data, const relocation_table& table, const memory_image& memory, std::size_t file);

		// Reads a pointer. Returns nullopt for a null pointer. Throws format_error if the value
		// doesn't match the next relocation entry, which means the reader got out of step.
		std::optional<memory_location> pointer();

		// Reads the next field as a pointer without moving on.
		std::optional<memory_location> peek_pointer();

		// Reads a pointer that must not be null.
		memory_location required_pointer();

		// Relocation entries not consumed yet.
		std::size_t pointers_left() const { return m_entries.size() - m_next; }

		// Finds where each relocation entry sits in the file, by matching values in order.
		// For exploring files whose layout isn't known yet.
		std::vector<std::size_t> scan() const;

	private:
		std::vector<relocation> m_entries;
		std::size_t m_next = 0;
		std::span<const std::byte> m_data;
		const memory_image& m_memory;
		std::size_t m_file;
	};
}
