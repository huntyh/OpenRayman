#pragma once

#include <openrayman/game/interpreter.h>
#include <openrayman/game/live_memory.h>
#include <openrayman/scene.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace openrayman::game
{
	// A first visit starts where the level's intro cutscene hands Rayman over, until the cutscenes
	// play (Phase 4). Learn_10: he crash-lands in the woods, looks for Globox and lands here in
	// behaviour 1 (the real game, tools/r2live.py, 2026-10-31); on a revisit node 4 teleports him to
	// the same spot (waypoint 0d/01 +0x46C7C).
	//
	// The intro is directed by an actor of the level (Learn_10: node 38, one of six actors sharing a
	// "scripted walk" model, 0d/01 +0x11BEC). While the flag holder (its var2, here the director
	// itself) has custom bit 28 and Rayman isn't in the portal arrival (a revisit), the director
	// hides Rayman, switches his brain off (custom bits 14 and 17) and lets a stand-in (node 40)
	// walk the scene to a waypoint; when the scene ends it clears that bit and takes itself out of
	// the world. We start after the scene, so we do the same. (Before 7defa8b its var3, a type-9
	// variable, didn't read back as Rayman, so the scene never took hold of him.)
	struct first_visit_hand_over
	{
		const char* level;
		float x, y, z, front_x, front_y; // where Rayman stands and which way he looks
		std::size_t director;            // the node directing the intro
		std::size_t scene_flag_holder;   // the actor whose custom bit 28 says the scene runs
	};

	inline constexpr first_visit_hand_over first_visit_hand_overs[] = {
		{ "Learn_10", 30.292f, 295.230f, -7.295f, -0.883f, 0.469f, 38, 38 },
	};

	// The story bits a start stands in for (a new game logged with `r2live.py --story-only`,
	// 2026-11-01). A new game sets 1089 at the end of the pirate ship level and 1091 at Learn_10's
	// hand-over, at the spot above (node 4 runs an intro effect until it is set). Finishing Learn_10
	// (jumping into the exit gate) sets 1133. A completed level also retains 1089 and 1091:
	// otherwise its director keeps the intro's full-screen cover up on a revisit.
	inline constexpr std::uint32_t first_visit_story_bits[] = { 1089, 1091 };
	inline constexpr std::uint32_t revisit_story_bits[] = { 1089, 1091, 1133 };

	// Writes a super object's transform: position, then its X, Y and Z axes (twice: rotation and the
	// combined matrix).
	inline void write_transform(live_memory& m, std::uint32_t super_object, const float (&at)[3], const float (&axes)[9])
	{
		std::uint32_t t = m.u32(super_object + 0x24);
		for(std::uint32_t k = 0; k < 3; k++)
			m.write<float>(t + 4 + k * 4, at[k]);
		for(std::uint32_t k = 0; k < 9; k++)
		{
			m.write<float>(t + 0x10 + k * 4, axes[k]);
			m.write<float>(t + 0x34 + k * 4, axes[k]);
		}
	}

	// Puts the main actor (`rayman`, a super object) where the level's intro leaves him, the game's
	// camera (`camera`, 0 for none) behind him as the intro ends, and ends the intro's director.
	// Returns false for levels without a known hand-over.
	inline bool apply_first_visit_hand_over(live_memory& m, interpreter& vm, const scene& tree, std::string_view level, std::uint32_t rayman, std::uint32_t camera = 0)
	{
		for(const first_visit_hand_over& h : first_visit_hand_overs)
		{
			if(!std::ranges::equal(level, std::string_view(h.level), [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }))
				continue;
			// His -Y axis is his front.
			write_transform(m, rayman, { h.x, h.y, h.z }, { -h.front_y, h.front_x, 0, -h.front_x, -h.front_y, 0, 0, 0, 1 });
			// The camera 6 units behind him and 2.5 up, looking along his front (its -Y looks): its
			// scripts and mechanics take it from there.
			if(camera)
				write_transform(m, camera, { h.x - 6 * h.front_x, h.y - 6 * h.front_y, h.z + 2.5f },
					{ -h.front_y, h.front_x, 0, -h.front_x, -h.front_y, 0, 0, 0, 1 });
			// The scene is over: the director's own ending (its behaviour 1: custom bit 28 of the
			// flag holder cleared, then procedure 139 on itself).
			if(h.director < tree.nodes().size())
				vm.remove(tree.node(h.director).at.address());
			if(h.scene_flag_holder < tree.nodes().size() && tree.node(h.scene_flag_holder).data)
			{
				std::uint32_t actor = tree.node(h.scene_flag_holder).data->address();
				std::uint32_t standard = m.contains(actor + 4, 4) ? m.u32(actor + 4) : 0;
				if(standard && m.contains(standard + 0x24, 4))
					m.write<std::uint32_t>(standard + 0x24, m.u32(standard + 0x24) & ~(1u << 27));
			}
			return true;
		}
		return false;
	}
}
