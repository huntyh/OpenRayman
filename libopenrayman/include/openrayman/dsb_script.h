#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace openrayman
{
	// Removes the DSB/SNA style XOR obfuscation. The first 4 bytes of the input are the
	// initial key and are not part of the output.
	std::vector<std::byte> decode_dsb(std::span<const std::byte> data);

	// Loading screen ("vignette") description.
	struct dsb_vignette
	{
		// Picture candidates. "Random<A,1,1,B,1,6>" style specs are expanded to their names.
		std::vector<std::string> images;
		std::array<float, 4> bar_outside_color{};
		std::array<float, 4> bar_inside_color{};
		std::array<std::array<float, 4>, 4> bar_corner_colors{};
		std::array<std::int32_t, 4> bar_rect{};
		std::int32_t bar_max = 0;
	};

	// The contents of a DSB script: Game.dsb describes the whole game, each level has its own.
	struct dsb_script
	{
		// Section 0x00: memory pool sizes, keyed by pool id.
		std::map<std::uint32_t, std::uint32_t> memory;

		// Section 0x1E: all levels in Game.dsb order. The first one is loaded on startup.
		std::vector<std::string> levels;

		// Section 0x28: data directories, keyed by role ("world", "levels", "sound", ...).
		std::map<std::string, std::string> directories;

		// Section 0x40: texture archives, e.g. "\Textures.cnt".
		std::vector<std::string> texture_archives;

		// Section 0x46: loading screens, in order of appearance.
		std::vector<dsb_vignette> vignettes;

		// Section 0x5C: sound banks to load.
		std::vector<std::uint32_t> sound_banks;

		// Section 0x64: options file names.
		std::string default_options_file;
		std::string current_options_file;
	};

	// Decodes and parses a DSB file. Throws format_error on unknown or malformed sections.
	dsb_script parse_dsb(std::span<const std::byte> data);
}
