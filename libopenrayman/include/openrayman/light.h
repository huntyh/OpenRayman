// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/geometry.h>
#include <openrayman/memory_image.h>
#include <openrayman/scene.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openrayman
{
	enum class light_type : std::uint32_t
	{
		parallel = 1,       // sunlight: shines along its matrix's Z axis
		spherical = 2,      // a point light, full strength up to `near`, gone at `far`
		ambient = 4,        // the same everywhere, whichever way a surface faces
		parallel_other = 5, // 11 in the whole game, not decoded yet: lit like a parallel light
		fog = 6,            // thickens with distance from the camera
	};

	// What a light shines on (bits of its "object lighted" byte).
	namespace light_target
	{
		constexpr std::uint8_t scenery = 0x1; // the level's static geometry
		constexpr std::uint8_t actors = 0x2;
	}

	// A light from a sector's static light list. See docs/formats/lighting.md.
	struct light
	{
		memory_location at;
		light_type type = light_type::ambient;
		bool on = true;
		float far = 0, near = 0;
		affine placement;             // position, and for parallel lights their direction
		std::array<float, 4> color{}; // can go above 1, or below 0 to darken
		std::uint8_t targets = 0;     // light_target bits
		bool painting = false;        // lights surfaces whichever way they face
		// Fog only: how much of the fog colour covers a surface at `near` and at `far` (0..1).
		float fog_near_blend = 0, fog_far_blend = 0;
		std::array<float, 4> background{}; // fog only: the colour behind everything

		vec3 position() const { return { placement.m[0][3], placement.m[1][3], placement.m[2][3] }; }
		// Parallel lights: which way the light travels.
		vec3 direction() const;
	};

	// A sector of the level: the static lights it lists and the box it covers.
	struct sector
	{
		std::size_t node = 0;              // the sector's super object
		std::vector<std::size_t> lights;   // indices into level_lights::lights
		vec3 min{}, max{};
		// Virtual sectors overlay others to light parts of the scenery; nothing stands in them.
		bool is_virtual = false;

		bool contains(const vec3& point) const;
	};

	struct level_lights
	{
		std::vector<light> lights; // each one once, though sectors share them
		std::vector<sector> sectors;

		// The sector a scene node belongs to (itself or its nearest sector ancestor).
		std::optional<std::size_t> sector_of(const scene& tree, std::size_t node) const;
		// The sector a point is in: the smallest box (not virtual) that holds it, or the nearest box.
		std::optional<std::size_t> sector_at(const vec3& point) const;
		// The fog light of a sector, if it has one that's on.
		const light* fog(std::size_t sector) const;
	};

	// Reads every sector's static lights. Throws format_error on lights of unknown types or
	// inconsistent lists.
	level_lights read_lights(const memory_image& memory, const scene& tree);

	// The light falling on a surface point with this normal (both in world space): the sum of every
	// light in the list that's on and shines on `target`, per colour channel, not clamped.
	vec3 light_at(std::span<const light* const> lights, std::uint8_t target, const vec3& position, const vec3& normal);
}
