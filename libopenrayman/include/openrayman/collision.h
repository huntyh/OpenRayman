// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/geometry.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace openrayman
{
	// Surface flags of a collide material (u16 at collide material +2). The meanings known so far
	// were confirmed in the game (docs/formats/collision.md); the other bits are still unknown.
	using surface_flags = std::uint16_t;
	namespace surface
	{
		constexpr surface_flags grabbable = 0x0004; // Rayman can hang from its edge (ledges, column tops)
		constexpr surface_flags water = 0x4000;
	}

	// One triangle the level collides with, in world space.
	struct collision_triangle
	{
		std::array<vec3, 3> corners{};
		vec3 normal{};               // unit length, from the data, turned into world space
		surface_flags flags = 0;     // 0 when the triangle has no collide material
		bool has_material = false;   // whether its game material has a collide material at all
		std::size_t node = 0;        // the super object it belongs to, in the scene
	};

	// The geometric object a physical object collides with: its collide set's fourth slot (+0xC).
	// Often the same mesh as the visible one.
	std::optional<memory_location> physical_object_collision(const memory_image& memory, const memory_location& physical);

	// Every collision triangle of the level's static geometry (instantiated physical objects in the
	// sector tree), placed in the world. Element types other than triangles are skipped: the level
	// geometry has none that collide. Throws format_error.
	std::vector<collision_triangle> static_collision(const memory_image& memory, const scene& tree);
}
