#pragma once

#include <openrayman/game/live_memory.h>
#include <openrayman/collision.h>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace openrayman::game
{
	// The level's collision triangles, bucketed by where they lie on the ground plane so an actor
	// only tests the few under it.
	class world_collision
	{
	public:
		world_collision() = default;
		explicit world_collision(std::vector<collision_triangle> triangles);

		bool empty() const { return m_triangles.empty(); }

		// A surface the actor met: the floor it stands on (floor_at) or the water around it
		// (water_at). height is the surface's Z at the point asked about.
		struct surface_hit
		{
			float height = 0;
			std::uint32_t material = 0;    // the triangle's game material, 0 when it has none
			std::array<float, 3> normal{}; // its unit normal in world space, pointing up and out
		};

		// The highest floor (a triangle facing up, at most 60° from flat, not water) under (x, y) with a height
		// in [bottom, top], or nothing.
		std::optional<surface_hit> floor_at(float x, float y, float top, float bottom) const;

		// The highest water triangle (collide flag 0x4000, at most 60° from flat) over (x, y) with a
		// height in [bottom, top], or nothing: the surface of the pond the actor is in (0x43F510).
		std::optional<surface_hit> water_at(float x, float y, float top, float bottom) const;

		// How far (x, y) must move to take a vertical cylinder of `radius` out of every wall (a
		// triangle steeper than 60°, not water) it overlaps between `bottom` and `top`; 0 when it is
		// free. Corners of two walls resolve over a few passes.
		std::array<float, 2> wall_push(float x, float y, float bottom, float top, float radius) const;

	private:
		// The triangles of the cell (x, y) lies in, or an empty list.
		const std::vector<std::uint32_t>& under(float x, float y) const;

		static constexpr float cell = 4.0f;
		std::vector<collision_triangle> m_triangles;
		std::map<std::pair<int, int>, std::vector<std::uint32_t>> m_cells;
	};

	// One tick of an actor's mechanics (0x431AD0, docs/formats/dynamics.md): the speeds the scripts
	// set and gravity give a target position, the floor stops it, and the result becomes the speed
	// the scripts read back (dynamics base +0x3C) and the contact word they test (conditions 27..32).
	void move_actor(live_memory& memory, std::uint32_t super_object, std::uint32_t actor, double ms, const world_collision& world);
}
