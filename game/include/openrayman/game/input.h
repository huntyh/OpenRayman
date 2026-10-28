#pragma once

#include <openrayman/input.h>
#include <openrayman/game/live_memory.h>

#include <array>
#include <cstdint>

namespace openrayman::game
{
	// Feeds the keyboard into the game's input actions, the way Rayman2.exe does every frame
	// (0x42E710): each enabled action's expression is evaluated and its counter moves on. Scripts
	// read the counters (conditions 44..47). Joystick tests are false for now.
	class input
	{
	public:
		input(live_memory& memory, const input_actions& actions);

		// A key went down or up (DirectInput scan code). Takes effect at the next step().
		void set_key(std::uint8_t scan_code, bool down);
		void release_all();

		// One 60 Hz tick.
		void step();

		// The address of action `index`, or 0 past the end.
		std::uint32_t action(std::size_t index) const;

	private:
		bool evaluate(std::uint32_t& at, std::uint32_t end);

		live_memory& m_memory;
		std::uint32_t m_first;
		std::size_t m_count;
		std::array<bool, 256> m_keys{};   // as the events left them
		std::array<bool, 256> m_now{};    // this tick
		std::array<bool, 256> m_before{}; // last tick
		std::array<std::uint8_t, 256> m_frames{}; // ticks since each key last changed, up to 255
	};
}
