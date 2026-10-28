// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/memory_image.h>

#include <cstdint>
#include <optional>
#include <string>

namespace openrayman
{
	// The game's input actions ("jump", "move left", ...). Fix.gpt names an array of them in Fix's
	// memory. Each one is a small expression over keys, joystick buttons and other actions, saved
	// as 8-byte slots in prefix order, which the game evaluates every frame. See
	// docs/formats/input.md.
	struct input_actions
	{
		memory_location first;  // the first action; the others follow, input_action_size apart
		std::size_t count = 0;  // 138 in every level
	};

	// An action: u32 slot count, pointer to the expression, pointer to a runtime-only block, i32
	// counter (frames held, negative frames since release), f32 analog value, u8 enabled.
	constexpr std::uint32_t input_action_size = 0x18;
	namespace input_action
	{
		constexpr std::uint32_t slots = 0x00;
		constexpr std::uint32_t expression = 0x04;
		constexpr std::uint32_t counter = 0x0C;
		constexpr std::uint32_t analog = 0x10;
		constexpr std::uint32_t enabled = 0x14;
	}

	// The first byte of an expression slot: what it tests. Keys are DirectInput scan codes.
	enum class input_kind : std::uint8_t
	{
		all = 1,             // both of the next two expressions
		any = 2,             // either of the next two
		not_ = 3,            // the next expression is false
		key_pressed = 4,     // went down this frame
		key_released = 5,    // went up this frame
		key_down = 6,        // down, for between min and max frames
		key_up = 7,          // up, for between min and max frames
		action_pressed = 8,  // another action's counter is 1
		action_released = 9, // ... is -1
		action_held = 10,    // ... is above 0
		action_free = 11,    // ... is below 0
		pad_axis = 16,       // a joystick axis in a range; also sets the action's analog value
		// 12..15 and 26 test joystick buttons.
		sequence = 33,        // the next u32 steps happened in order (cheat codes), from a key history
		sequence_key = 34,    // a step: a key went down
		sequence_release = 35 // a step: a key went up (36, 37: the same for a joystick)
	};

	// Reads where the actions are from Fix.gpt. Throws format_error.
	input_actions read_input_actions(const level_files& files);

	// How many slots a node of this kind takes, its operands included but not the expressions it
	// combines (all, any, not). nullopt for kinds not read yet.
	std::optional<std::size_t> input_node_slots(std::uint8_t kind);

	// The action's expression as text, e.g. "any(key LShift, key 0x47)". Unknown kinds show as
	// "kind N" and end the text.
	std::string describe_input_action(const memory_image& memory, const input_actions& actions, const memory_location& action);

	// A DirectInput scan code's key name ("Up", "A", "Space"), or its number in hex.
	std::string key_name(std::uint16_t scan_code);
}
