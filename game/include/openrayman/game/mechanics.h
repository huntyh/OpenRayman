#pragma once

#include <openrayman/game/live_memory.h>
#include <openrayman/collision.h>

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

		// The highest floor (a triangle facing up, at most 60° from flat, not water) under (x, y) with a height
		// in [bottom, top], or nothing.
		std::optional<float> floor_at(float x, float y, float top, float bottom) const;

	private:
		static constexpr float cell = 4.0f;
		std::vector<collision_triangle> m_triangles;
		std::map<std::pair<int, int>, std::vector<std::uint32_t>> m_cells;
	};

	// One tick of an actor's mechanics (0x431AD0, docs/formats/dynamics.md): the speeds the scripts
	// set and gravity give a target position, the floor stops it, and the result becomes the speed
	// the scripts read back (dynamics base +0x3C) and the contact word they test (conditions 27..32).
	void move_actor(live_memory& memory, std::uint32_t super_object, std::uint32_t actor, double ms, const world_collision& world);
}
