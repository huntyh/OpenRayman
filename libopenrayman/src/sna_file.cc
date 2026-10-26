// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/sna_file.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/mask.h>

#include <algorithm>
#include <format>

// Rayman 2 PC SNA layout, after removing the mask (whose seed is the first 4 bytes):
//
//   u8 module, u8 id, u8 unknown, i32 base address
//   if base != -1:
//     u32 reserved end, u32 unknown, u32 unknown, u32 data size, data
//
// The same block can appear twice: first as an empty entry announcing the memory it needs,
// then again with its data.

namespace openrayman
{
	std::string block_id::to_string() const
	{
		return std::format("{:02x}/{:02x}", module, id);
	}

	sna_file::sna_file(std::span<const std::byte> raw)
	{
		binary_reader header(raw);
		openrayman::mask key(header.u32());
		std::vector<std::byte> decoded(raw.begin() + 4, raw.end());
		key.decode(decoded);

		binary_reader r(decoded);
		while(!r.at_end())
		{
			sna_block block;
			block.id.module = r.u8();
			block.id.id = r.u8();
			block.unknown = r.u8();
			std::int32_t base = r.i32();
			if(base == -1)
				continue;

			block.base = static_cast<std::uint32_t>(base);
			block.reserved_end = r.u32();
			r.u32();
			r.u32();
			std::uint32_t size = r.u32();
			auto data = r.bytes(size);
			block.data.assign(data.begin(), data.end());

			// Blocks are announced first without data, and data blocks repeat the announcement.
			if(std::ranges::find(m_regions, block.id, &sna_region::id) == m_regions.end())
				m_regions.push_back({ block.id, block.base, block.reserved_end });

			if(!block.data.empty())
			{
				if(find(block.id))
					throw format_error("SNA block " + block.id.to_string() + " has data twice");
				m_blocks.push_back(std::move(block));
			}
		}
	}

	const sna_block* sna_file::find(block_id id) const
	{
		for(const sna_block& block : m_blocks)
		{
			if(block.id == id)
				return &block;
		}
		return nullptr;
	}
}
