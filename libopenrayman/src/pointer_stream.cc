// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/pointer_stream.h>
#include <openrayman/error.h>

#include <cstring>
#include <format>

namespace openrayman
{
	pointer_stream::pointer_stream(std::span<const std::byte> data, const relocation_table& table, const memory_image& memory, std::size_t file)
		: binary_reader(data), m_data(data), m_memory(memory), m_file(file)
	{
		// The table files hold a single list, filed under block 0a/00.
		for(const auto& [id, list] : table.blocks)
			m_entries.insert(m_entries.end(), list.begin(), list.end());
	}

	std::optional<memory_location> pointer_stream::pointer()
	{
		std::size_t at = position();
		std::uint32_t value = u32();
		if(value == 0)
			return std::nullopt;
		if(m_next == m_entries.size())
			throw format_error(std::format("pointer 0x{:08X} at 0x{:X} has no relocation entry left", value, at));

		const relocation& entry = m_entries[m_next++];
		if(entry.address != value)
			throw format_error(std::format("pointer at 0x{:X} is 0x{:08X}, but the relocation table expects 0x{:08X}", at, value, entry.address));
		auto target = m_memory.resolve(m_file, entry.target, value);
		if(!target)
			throw format_error(std::format("pointer 0x{:08X} at 0x{:X} lies outside block {}", value, at, entry.target.to_string()));
		return target;
	}

	std::optional<memory_location> pointer_stream::peek_pointer()
	{
		std::size_t at = position();
		std::size_t next = m_next;
		auto result = pointer();
		seek(at);
		m_next = next;
		return result;
	}

	memory_location pointer_stream::required_pointer()
	{
		std::size_t at = position();
		auto result = pointer();
		if(!result)
			throw format_error(std::format("unexpected null pointer at 0x{:X}", at));
		return *result;
	}

	void pointer_stream::skip_to(std::size_t target)
	{
		if(target < position())
			throw format_error("pointer stream can't skip backwards");
		std::vector<std::size_t> offsets = scan();
		while(m_next < offsets.size() && offsets[m_next] < target)
			m_next++;
		seek(target);
	}

	std::vector<std::size_t> pointer_stream::scan() const
	{
		std::vector<std::size_t> offsets;
		std::size_t from = 0;
		for(const relocation& entry : m_entries)
		{
			std::size_t at = from;
			for(; at + 4 <= m_data.size(); at++)
			{
				std::uint32_t word;
				std::memcpy(&word, m_data.data() + at, 4);
				if(word == entry.address)
					break;
			}
			if(at + 4 > m_data.size())
				throw format_error(std::format("relocation entry 0x{:08X} not found in the file", entry.address));
			offsets.push_back(at);
			from = at + 4;
		}
		return offsets;
	}
}
