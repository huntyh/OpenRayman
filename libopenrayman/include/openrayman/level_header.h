// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/memory_image.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace openrayman
{
	// A position and orientation as the engine stores it (0x58 bytes). Field names follow the
	// usual OpenSpace layout; what `type` selects isn't confirmed yet.
	struct transform
	{
		std::uint32_t type = 0;
		std::array<float, 3> position{};
		std::array<float, 9> rotation{}; // row major
		std::array<float, 9> scale{};    // row major
	};

	// Where an actor stored in Fix (Rayman, the camera, ...) sits in this level. Fix is saved
	// without knowing the level, so its super objects have no links; the level GPT supplies them.
	struct fix_actor_placement
	{
		memory_location actor;
		memory_location super_object;
		transform matrix;
		std::uint32_t draw_flags = 0;
		float transparency = 0;
		std::optional<memory_location> next, previous, parent;
	};

	// The start of a level's GPT file: how Fix's actors join the level, and the roots of the
	// level's super object tree. See docs/formats/gpt.md.
	struct level_header
	{
		// Every actor stored in Fix, in file order, placed in this level or not.
		std::vector<memory_location> fix_actors;
		std::vector<fix_actor_placement> placements;

		memory_location actual_world;           // parent of the three below
		memory_location dynamic_world;          // actors
		memory_location inactive_dynamic_world;
		memory_location father_sector;          // the level geometry, as a tree of sectors

		// The placement for a Fix super object, or nullptr.
		const fix_actor_placement* placement_of(const memory_location& super_object) const;
	};

	// Reads the header of the level GPT in `files`. Throws format_error.
	level_header read_level_header(const level_files& files);
}
