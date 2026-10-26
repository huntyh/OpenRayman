// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/gf_texture.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <algorithm>
#include <string>

namespace openrayman
{
	// Signature values seen in Rayman 2 PC. They match the pixel format: 888 and 8888.
	constexpr std::uint32_t gf_format_888 = 888;
	constexpr std::uint32_t gf_format_8888 = 8888;

	// Decodes one RLE plane: a byte equal to repeat_byte is followed by (value, count).
	static void read_plane(binary_reader& reader, std::uint8_t repeat_byte, std::vector<std::uint8_t>& plane)
	{
		std::size_t at = 0;
		while(at < plane.size())
		{
			std::uint8_t value = reader.u8();
			if(value == repeat_byte)
			{
				value = reader.u8();
				std::size_t count = reader.u8();
				// Runs never cross into the next plane, but don't trust the file on that.
				count = std::min(count, plane.size() - at);
				std::fill_n(plane.begin() + static_cast<std::ptrdiff_t>(at), count, value);
				at += count;
			}
			else
				plane[at++] = value;
		}
	}

	gf_texture decode_gf(std::span<const std::byte> data)
	{
		binary_reader reader(data);

		std::uint32_t format = reader.u32();
		if(format != gf_format_888 && format != gf_format_8888)
			throw format_error("unsupported GF format " + std::to_string(format));

		gf_texture texture;
		texture.width = reader.u32();
		texture.height = reader.u32();
		texture.channels = reader.u8();
		std::uint8_t repeat_byte = reader.u8();

		if(texture.width == 0 || texture.height == 0 || texture.width > 4096 || texture.height > 4096)
			throw format_error("GF texture has implausible size");
		if(texture.channels != 3 && texture.channels != 4)
			throw format_error("GF texture has " + std::to_string(texture.channels) + " channels, expected 3 or 4");

		std::size_t pixels = static_cast<std::size_t>(texture.width) * texture.height;
		std::vector<std::uint8_t> planes[4] = {
			std::vector<std::uint8_t>(pixels),       // blue
			std::vector<std::uint8_t>(pixels),       // green
			std::vector<std::uint8_t>(pixels),       // red
			std::vector<std::uint8_t>(pixels, 255),  // alpha
		};
		for(std::size_t c = 0; c < texture.channels; c++)
			read_plane(reader, repeat_byte, planes[c]);

		texture.rgba.resize(pixels * 4);
		for(std::uint32_t y = 0; y < texture.height; y++)
		{
			// Flip vertically: GF is stored bottom-up.
			std::size_t src_row = static_cast<std::size_t>(texture.height - 1 - y) * texture.width;
			std::uint8_t* dst = texture.rgba.data() + static_cast<std::size_t>(y) * texture.width * 4;
			for(std::uint32_t x = 0; x < texture.width; x++)
			{
				std::size_t src = src_row + x;
				dst[x * 4 + 0] = planes[2][src];
				dst[x * 4 + 1] = planes[1][src];
				dst[x * 4 + 2] = planes[0][src];
				dst[x * 4 + 3] = planes[3][src];
			}
		}
		return texture;
	}
}
