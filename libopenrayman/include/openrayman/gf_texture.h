// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openrayman
{
	// A decoded GF texture as 8-bit RGBA, rows ordered top to bottom.
	struct gf_texture
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		// Channels stored in the file: 3 (BGR) or 4 (BGRA). Alpha is 255 for 3 channel textures.
		std::uint8_t channels = 0;

		// width * height * 4 bytes.
		std::vector<std::uint8_t> rgba;
	};

	// Decodes a GF (graphics file) texture. Throws format_error on malformed data.
	// GF stores each colour channel as a separate run-length encoded plane, bottom row first.
	gf_texture decode_gf(std::span<const std::byte> data);
}
