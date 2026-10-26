// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/texture_table.h>
#include <openrayman/error.h>
#include <openrayman/pointer_stream.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace openrayman
{
	namespace field
	{
		constexpr std::uint32_t flags = 0x14;
		constexpr std::uint32_t size = 0x1C; // u16 height, u16 width (0x18 holds a similar pair, often half)
		constexpr std::uint32_t name = 0x46;
		constexpr std::uint32_t name_length = 0x80;
	}

	// PTX files start with the number of texture slots the engine has, then the slots used.
	constexpr std::uint32_t slot_capacity = 1024;

	std::string texture_info::cnt_path() const
	{
		std::string path = name;
		std::ranges::replace(path, '\\', '/');
		if(auto dot = path.rfind('.'); dot != std::string::npos && path.find('/', dot) == std::string::npos)
			path.resize(dot);
		return path + ".gf";
	}

	texture_info read_texture_info(const memory_image& memory, const memory_location& at)
	{
		const auto& data = at.block->data;
		if(at.offset + field::name + field::name_length > data.size())
			throw format_error(std::format("texture at {} +0x{:X} lies outside loaded data", at.block->id.to_string(), at.offset));

		texture_info info;
		info.at = at;
		info.flags = memory.read_u32(at + field::flags);
		info.tiling = std::uint8_t(data[at.offset + field::name - 1]);
		std::uint32_t size = memory.read_u32(at + field::size);
		info.height = std::uint16_t(size);
		info.width = std::uint16_t(size >> 16);

		const char* name = reinterpret_cast<const char*>(data.data() + at.offset + field::name);
		info.name.assign(name, strnlen(name, field::name_length));
		// Names are Latin-1 ("fe_m\xE8che_nz.tga"), so only control characters mean garbage.
		if(info.name.empty() || !std::ranges::all_of(info.name, [](unsigned char c) { return c >= 0x20 && c != 0x7F; }))
			throw format_error(std::format("texture at {} +0x{:X} has no readable name", at.block->id.to_string(), at.offset));
		return info;
	}

	std::vector<texture_info> read_texture_table(const level_files& files, bool level)
	{
		pointer_stream stream(level ? files.level_ptx : files.fix_ptx, level ? files.level_ptx_table : files.fix_ptx_table,
			files.memory, level ? 1 : 0);

		if(stream.u32() != slot_capacity)
			throw format_error("PTX file doesn't start with the texture slot capacity");
		std::uint32_t count = stream.u32();
		if(count > slot_capacity)
			throw format_error("PTX file uses more texture slots than the engine has");

		// What follows the slots (one small value per slot) isn't understood yet.
		std::vector<texture_info> textures;
		for(std::uint32_t slot = 0; slot < count; slot++)
		{
			if(auto at = stream.pointer())
			{
				texture_info info = read_texture_info(files.memory, *at);
				info.slot = slot;
				textures.push_back(std::move(info));
			}
		}
		if(stream.pointers_left() != 0)
			throw format_error("PTX file has pointers after its texture slots");
		return textures;
	}
}
