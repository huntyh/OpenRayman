// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/animation.h>
#include <openrayman/geometry.h>
#include <openrayman/scene.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace openrayman
{
	// One entry of an actor's object list: the parts its animations can show.
	struct actor_object
	{
		std::optional<memory_location> physical_object;
		std::optional<vec3> scale;
	};

	// The dark blob an actor casts on the ground below it.
	struct actor_shadow
	{
		visual_material material; // Fix's shadow1_mxy_aaa in every case seen
		float width = 1, length = 1;  // the blob's size along the actor's X and Y
	};

	// An actor ("perso"): what it looks like and which animation its current state plays.
	// See docs/formats/animation.md.
	struct actor
	{
		std::size_t node = 0; // its super object, in the scene
		std::optional<std::uint16_t> animation; // in the animation bank, if its state has one
		std::vector<actor_object> objects;
		// Its family's states, in order, with the animation each plays, and which one it's in. The
		// saved state is often a placeholder that the game's scripts replace when the level starts.
		std::optional<memory_location> family;
		std::vector<std::optional<std::uint16_t>> states;
		std::optional<std::size_t> state;
		std::optional<actor_shadow> shadow;
	};

	// Reads the actor behind a super object of type actor. Throws format_error.
	actor read_actor(const memory_image& memory, const scene& tree, const animation_bank& bank, std::size_t node);

	// The world transform of a super object: its own and all its parents' transforms. Fix's actors
	// take theirs from the level header.
	affine world_transform(const memory_image& memory, const scene& tree, std::size_t node);

	// The actors of the dynamic world, ready to animate: each distinct mesh is read once, and an
	// actor's object list refers to them by index.
	struct actor_set
	{
		struct entry
		{
			std::size_t node = 0;
			affine world;
			std::optional<std::uint16_t> animation; // what it plays now
			std::optional<memory_location> family;  // see actor
			std::vector<std::optional<std::uint16_t>> states;
			std::optional<std::size_t> state;
			std::vector<std::optional<std::size_t>> meshes; // per object list entry, if it has one
			std::vector<std::optional<vec3>> scales;        // per object list entry
			std::optional<actor_shadow> shadow;
		};
		std::vector<geometric_object> meshes;
		std::vector<entry> actors;
	};

	// Reads every actor in the dynamic world. Throws format_error.
	actor_set read_actors(const memory_image& memory, const scene& tree, const animation_bank& bank);

	// One part of a posed actor: which mesh, and where.
	struct posed_part
	{
		std::size_t node = 0; // the actor's super object
		std::size_t mesh = 0; // in actor_set::meshes
		affine world;
	};

	// Appends an actor's parts at a frame of its current animation (see pose()). Actors without an
	// animation show their first object, if they have one.
	void pose_actor(const actor_set::entry& actor, const animation_bank& bank, double frame, std::vector<posed_part>& out);

	// Geometry of every actor in the dynamic world, posed at the first frame of its current
	// animation.
	std::vector<placed_geometry> actor_geometry(const memory_image& memory, const scene& tree, const animation_bank& bank);
}
