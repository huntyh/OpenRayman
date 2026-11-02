#pragma once

#include <openrayman/game/live_memory.h>
#include <openrayman/collision.h>

#include <array>
#include <functional>
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
		explicit world_collision(std::vector<collision_triangle> triangles, bool actor_geometry = false);

		bool empty() const { return m_triangles.empty(); }

		// A surface the actor met: the floor it stands on (floor_at) or the water around it
		// (water_at). height is the surface's Z at the point asked about.
		struct surface_hit
		{
			float height = 0;
			std::uint32_t material = 0;    // the triangle's game material, 0 when it has none
			std::array<float, 3> normal{}; // its unit normal in world space, pointing up and out
			std::uint32_t actor = 0; // owning super object for an actor surface, 0 for scenery
		};

		// The highest floor (a triangle facing up, at most 60° from flat, not water) under (x, y) with a height
		// in [bottom, top], or nothing.
		std::optional<surface_hit> floor_at(float x, float y, float top, float bottom) const;

		// The lowest ceiling (a triangle facing down, at most 60° from flat) over (x, y) with a height in
		// [bottom, top], or nothing.
		std::optional<surface_hit> ceiling_at(float x, float y, float top, float bottom) const;

		// The highest water triangle (collide flag 0x4000, at most 60° from flat) over (x, y) with a
		// height in [bottom, top], or nothing: the surface of the pond the actor is in (0x43F510).
		std::optional<surface_hit> water_at(float x, float y, float top, float bottom) const;

		// How far (x, y) must move to take a vertical cylinder of `radius` out of every wall (a
		// triangle steeper than 60°, not water) it overlaps between `bottom` and `top`; 0 when it is
		// free. Corners of two walls resolve over a few passes.
		std::array<float, 2> wall_push(float x, float y, float bottom, float top, float radius) const;

		// The first surface the segment from `start` to `end` meets (water and 0x8000 surfaces let
		// it through), with the point it meets it at, or nothing (0x474780 over the level geometry).
		struct segment_hit
		{
			std::array<float, 3> point{};
			std::array<float, 3> normal{}; // the surface's unit normal
			std::uint32_t material = 0;
		};
		// With `any_of`, only surfaces carrying one of those collide flags count (function 201's mask,
		// 0x4B9C44 tested at 0x48E820).
		std::optional<segment_hit> first_hit(const std::array<float, 3>& start, const std::array<float, 3>& end, std::uint16_t any_of = 0) const;

		// Moves a sphere of `radius` from `start` towards `end` and slides it along what it meets, the
		// way the exe moves its camera (0x434660): it stops 0.02 short of a surface and carries on
		// with what is left of the movement along it (along the crease of two), at most five times,
		// and gives up when the slide turns more than 150 degrees from the movement. A sphere that
		// starts inside a surface is pushed out first. Water and 0x8000 surfaces let it through
		// (0x434390 skips collide flags 0xC000). Returns where it ends.
		std::array<float, 3> slide_sphere(const std::array<float, 3>& start, const std::array<float, 3>& end, float radius) const;

		// The nearest surface carrying all of `flags` that a sphere at `centre` touches: the point on
		// it closest to the centre, within `radius`, its normal and material. What a ledge probe of
		// Rayman's finds (his collision zones with flag 0x8 against grabbable 0x4 surfaces).
		// `accept`, when given, can turn a surface down (it then doesn't count as the nearest).
		std::optional<segment_hit> sphere_touch(const std::array<float, 3>& centre, float radius, std::uint16_t flags,
			const std::function<bool(const segment_hit&)>& accept = {}) const;

	private:
		// The triangles of the cell (x, y) lies in, or an empty list.
		const std::vector<std::uint32_t>& under(float x, float y) const;

		static constexpr float cell = 4.0f;
		std::vector<collision_triangle> m_triangles;
		bool m_actor_geometry = false;
		std::map<std::pair<int, int>, std::vector<std::uint32_t>> m_cells;
	};

	class interpreter;
	// The supporting actor's placement when the rider last moved. Kept separately from scene
	// links because our actors store world transforms directly (the exe reparents at 0x43D950).
	struct moving_support
	{
		std::uint32_t actor = 0;
		affine placement;
	};
	// One actor's surfaces in world space, kept until it moves, changes state or frame, or switches
	// zones: every mover that collides asks for them each tick, and posing them each time halved
	// the frame rate.
	struct actor_surfaces
	{
		affine placement;
		std::uint32_t state = 0, objects = 0, zones = 0, group = 0, modes = 0;
		std::uint16_t frame = 0;
		std::uint32_t sector = 0;
		std::vector<collision_triangle> triangles;
		vec3 low{}, high{}; // the triangles' bounds
	};
	// Each actor's surfaces, and which actors have any: looked for once per tick, since only a few
	// (cages, the trapdoor) do, then only those are checked for each mover.
	struct actor_surface_cache
	{
		std::map<std::uint32_t, actor_surfaces> actors;
		std::vector<std::uint32_t> owners;
		std::optional<std::uint32_t> clock; // the tick owners was found in
	};
	// Live kind-3 zones and posed physical parts, excluding the mover itself (0x494420). Actors
	// whose surfaces are further than `reach` from the mover are left out.
	constexpr float actor_surface_reach = 10.0f;
	std::vector<collision_triangle> actor_collision_triangles(interpreter& vm, std::uint32_t mover);
	// One tick of an actor's mechanics (0x431AD0, docs/formats/dynamics.md): the speeds the scripts
	// set and gravity give a target position, the floor stops it, and the result becomes the speed
	// the scripts read back (dynamics base +0x3C) and the contact word they test (conditions 27..32).
	void move_actor(live_memory& memory, std::uint32_t super_object, std::uint32_t actor, double ms, const world_collision& world, interpreter* vm = nullptr);
}
