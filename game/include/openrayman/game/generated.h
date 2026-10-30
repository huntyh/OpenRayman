#pragma once

#include <openrayman/game/live_memory.h>

#include <array>
#include <cstdint>
#include <vector>

namespace openrayman::game
{
	// Actors the scripts create while the game runs (function 66), Rayman's shots among them: copies
	// of a template actor that isn't in the scene. The game keeps them in one shared pool of slots
	// (0x4A6B18 count, 0x4A6B28 super objects, 0x4A6B30 who made each) and fills a free slot by
	// copying the template part by part (0x40BCC0); procedure 139 frees the slot again, and the next
	// copy reuses its memory. See docs/formats/generated.md.
	class generated_actors
	{
	public:
		struct slot
		{
			std::uint32_t super_object = 0; // its super object, once the slot was first used
			std::uint32_t creator = 0;      // whose scripts made it (super object); 0: the slot is free
			std::uint32_t model = 0;        // the template actor it was copied from
			std::vector<std::uint32_t> memory; // the blocks the copy got, in order, reused next time
		};

		// How many can exist at once: 40 in the real game's Learn_10 (0x4A6B18, read live with
		// tools/r2live.py --generated). Where the game takes it from isn't read yet.
		static constexpr std::size_t capacity = 40;

		// Copies `model` (an actor) into a free slot, placed at `at` with no rotation, made by
		// `creator`. Returns the new super object (type 2, actor at +4), or 0 when the pool is full or
		// `model` isn't an actor.
		std::uint32_t create(live_memory& m, std::uint32_t model, std::uint32_t creator, const std::array<float, 3>& at, std::uint32_t clock);
		// Frees the slot of a generated super object. False if it isn't one.
		bool release(std::uint32_t super_object);
		// How many live ones `creator` made (function 67, 0x40C2A0).
		std::size_t count(std::uint32_t creator) const;
		// The live ones' super objects, in slot order.
		std::vector<std::uint32_t> alive() const;
		const std::vector<slot>& slots() const { return m_slots; }

	private:
		std::vector<slot> m_slots;
	};
}
