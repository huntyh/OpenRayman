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

	// An actor ("perso"): what it looks like and which animation its current state plays.
	// See docs/formats/animation.md.
	struct actor
	{
		std::size_t node = 0; // its super object, in the scene
		std::optional<std::uint16_t> animation; // in the animation bank, if its state has one
		std::vector<actor_object> objects;
	};

	// Reads the actor behind a super object of type actor. Throws format_error.
	actor read_actor(const memory_image& memory, const scene& tree, const animation_bank& bank, std::size_t node);

	// The world transform of a super object: its own and all its parents' transforms. Fix's actors
	// take theirs from the level header.
	affine world_transform(const memory_image& memory, const scene& tree, std::size_t node);

	// Geometry of every actor in the dynamic world, posed at the first frame of its current
	// animation. Actors without an animation show their first object, if they have one.
	std::vector<placed_geometry> actor_geometry(const memory_image& memory, const scene& tree, const animation_bank& bank);
}
