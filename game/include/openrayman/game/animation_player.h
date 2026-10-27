#pragma once

#include <openrayman/game/interpreter.h>
#include <openrayman/game/live_memory.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <span>

namespace openrayman::game
{
	// Where an actor is in its animations, kept in its 3D data as the game does (0x40E9E0, 0x40FAA0).
	namespace data_3d
	{
		constexpr std::uint32_t current_state = 0x04;   // the state playing now
		constexpr std::uint32_t requested_state = 0x08; // the one the scripts asked for
		constexpr std::uint32_t frame = 0x74;           // u16
		constexpr std::uint32_t loops = 0x76;           // u8: plays left before moving on
		constexpr std::uint32_t previous_state = 0x84;
		constexpr std::uint32_t finished = 0x8E;        // u8: the animation played to its end
		constexpr std::uint32_t next_state = 0x88;      // what plays next: requested, or a transition on the way
		constexpr std::uint32_t elapsed = 0xC0;         // ms into the current animation
		constexpr std::uint32_t changed_at = 0xC4;      // game clock when the state changed
		constexpr std::uint32_t changed = 0xE1;         // u8: a change waits to be played
	}

	// A family state.
	namespace state
	{
		constexpr std::uint32_t animation = 0x04;   // animation reference: u16 frames, u8 frames per second
		constexpr std::uint32_t transitions = 0x08; // list: next, target state, state to play on the way
		constexpr std::uint32_t forbidden = 0x14;   // list: next, state; count at +0x1C
		constexpr std::uint32_t forbidden_count = 0x1C;
		constexpr std::uint32_t after = 0x20;       // where to go when the animation ends
		constexpr std::uint32_t transitions_only = 0x2A; // u8: 1 = may leave only through a transition
	}

	// Switches an actor (its 3D data) to a state as procedure 72 does: through the transition the
	// current state lists for it, if any; refused (false) when the current state forbids it or allows
	// leaving only by transitions. `forced` skips those rules. The new state plays at once.
	bool change_state(live_memory& memory, std::uint32_t data, std::uint32_t target, bool forced, std::uint32_t clock);

	// Advances an actor's animation by one tick of `ms`; at the end of an animation it plays again
	// (while loops remain) or moves on to the state's "after" state.
	void play_animation(live_memory& memory, std::uint32_t data, std::uint32_t ms);

	// One tick of the game (1/60 s): the clock moves on, every actor thinks, then every actor's
	// animation advances. Actors taken out of the world are skipped. An actor whose scripts fail
	// is reported to `on_error` (if given; else the error propagates) and the others carry on.
	using step_error = std::function<void(std::uint32_t super_object, const std::exception&)>;
	void step(interpreter& vm, std::span<const std::uint32_t> super_objects, const step_error& on_error = {});
}
