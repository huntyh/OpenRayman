#pragma once

#include <openrayman/input.h>
#include <openrayman/game/live_memory.h>

#include <array>
#include <cstdint>
#include <deque>

namespace openrayman::game
{
	// Feeds the keyboard and the gamepads into the game's input actions, the way Rayman2.exe does
	// every frame (0x42E710): each enabled action's expression is evaluated and its counter moves
	// on. Scripts read the counters (conditions 44..47) and the analog value joystick axes set.
	class input
	{
	public:
		// Pads and their buttons and axes, as the game numbers them: four pads, buttons 0..41, and
		// axes 4..9 (the first six axes of a pad, -32768..32767 raw).
		static constexpr std::size_t pad_count = 4;
		static constexpr std::size_t pad_button_count = 42;
		static constexpr std::size_t pad_first_axis = 4;
		static constexpr std::size_t pad_axis_count = 6;

		input(live_memory& memory, const input_actions& actions);

		// A key went down or up (DirectInput scan code). Takes effect at the next step().
		void set_key(std::uint8_t scan_code, bool down);

		// A pad button went down or up (the game's button number). Takes effect at the next step().
		void set_pad_button(std::size_t pad, std::size_t button, bool down);

		// A pad axis moved: the game's axis number (4..9) and the raw value, -32768..32767.
		void set_pad_axis(std::size_t pad, std::size_t axis, std::int32_t value);

		// Everything up: a window that lost the focus, or a pad that went away.
		void release_all();
		// Ordered virtual presses for pointer/wheel navigation, merged with held physical keys.
		void pulse_key(std::uint8_t scan_code) { m_presses.push_back(scan_code); }
		void release_pad(std::size_t pad);

		// One 60 Hz tick.
		void step();

		// Whether a key (DirectInput scan code) is down this tick.
		bool key(std::uint8_t scan_code) const { return m_now[scan_code]; }

		// Whether a pad button is down this tick.
		bool pad_button(std::size_t pad, std::size_t button) const;

		// A pad axis this tick, -32768..32767 (0 for a pad or axis that doesn't exist).
		std::int32_t pad_axis(std::size_t pad, std::size_t axis) const;

		// The address of action `index`, or 0 past the end.
		std::uint32_t action(std::size_t index) const;

	private:
		bool evaluate(std::uint32_t& at, std::uint32_t end);
		bool pad_button_test(std::uint8_t kind, std::int16_t pad, std::int16_t button, std::uint8_t lo, std::uint8_t hi) const;

		live_memory& m_memory;
		std::uint32_t m_first;
		std::size_t m_count;
		std::deque<std::uint8_t> m_presses;
		unsigned m_press_ticks = 0;
		std::uint8_t m_press_key = 0;
		bool m_press_gap = false;
		std::array<bool, 256> m_keys{};   // as the events left them
		std::array<bool, 256> m_now{};    // this tick
		std::array<bool, 256> m_before{}; // last tick
		std::array<std::uint8_t, 256> m_frames{}; // ticks since each key last changed, up to 255

		// The same three for each pad's buttons, plus the axes (they have no edges: only the value
		// matters, so the events replace it).
		std::array<std::array<bool, pad_button_count>, pad_count> m_pad_buttons{};
		std::array<std::array<bool, pad_button_count>, pad_count> m_pad_now{};
		std::array<std::array<bool, pad_button_count>, pad_count> m_pad_before{};
		std::array<std::array<std::uint8_t, pad_button_count>, pad_count> m_pad_frames{};
		std::array<std::array<std::int32_t, pad_axis_count>, pad_count> m_pad_axes{};

		// The action being evaluated, so a joystick axis can set its analog value.
		std::uint32_t m_analog_action = 0;
	};
}
