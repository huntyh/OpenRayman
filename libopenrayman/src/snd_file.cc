// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/snd_file.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/file.h>

#include <format>

namespace openrayman
{
	std::vector<sound_slot> read_snd(std::span<const std::byte> data)
	{
		if(data.size() % 8)
			throw format_error(std::format("a .snd file holds 8-byte pairs, this one has {} bytes", data.size()));
		std::vector<sound_slot> pairs;
		binary_reader r(data);
		for(std::size_t i = 0; i < data.size() / 8; i++)
		{
			sound_slot s;
			s.event = r.u32();
			s.slot = r.u32();
			pairs.push_back(s);
		}
		return pairs;
	}

	std::vector<sound_slot> read_snd(const std::filesystem::path& path)
	{
		std::vector<std::byte> data = read_file(path);
		return read_snd(std::span<const std::byte>(data));
	}
}
