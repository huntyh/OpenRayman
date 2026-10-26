// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/memory_image.h>

#include <cstdint>
#include <string>
#include <vector>

namespace openrayman
{
	// A texture as the engine describes it in memory. Materials point at these.
	struct texture_info
	{
		memory_location at;
		std::uint32_t slot = 0; // index in the PTX file
		// As stored, e.g. "textures_temple_terre\temple_terre_murfaille_txy.tga". The file itself
		// is in a CNT archive with a .gf extension, see cnt_path().
		std::string name;
		std::uint32_t flags = 0;
		// The size the engine expects. For about 3% of textures the GF file in the archive is
		// half or double this in some direction; the GF file's own size is what to render with.
		std::uint16_t width = 0, height = 0;

		// The path of the texture inside Textures.cnt or Vignette.cnt.
		std::string cnt_path() const;
	};

	// Reads a PTX file: the textures Fix or a level loads. `level` picks the level's file
	// instead of Fix's. Throws format_error. See docs/formats/gpt.md.
	std::vector<texture_info> read_texture_table(const level_files& files, bool level);

	// Reads the texture description at a location in memory.
	texture_info read_texture_info(const memory_image& memory, const memory_location& at);
}
