#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace openrayman::game
{
	// The game's on-screen text: 50 slots filled by procedures 505..507 and drawn every frame by
	// the exe's text pass (0x465C10). Swapping and drawing are the renderer's job (nothing draws
	// them yet); this keeps the state the scripts build. Table 0x4E1080, slot size 0x1C, and the
	// flag bits below are the exe's own; see docs/formats/menu.md.
	class screen_text
	{
	public:
		static constexpr std::size_t slot_count = 50;

		struct slot
		{
			bool used = false;         // +0x00 was non-zero: a string was set
			std::string text;          // +0x00, the bytes the draw pass reads
			float x = 0;               // +0x04
			float y = 0;               // +0x08
			float size = 0;            // +0x0C
			std::uint8_t alpha = 0;    // +0x10
			std::uint8_t wobble = 0;   // +0x12: non-zero makes the text sway
			std::uint8_t flags = 0;    // +0x13
			std::uint8_t word = 0;     // +0x14: the word index being flashed
			std::uint8_t colour = 0;   // +0x18: 0 default, 1..2 red, 3 yellow
		};

		// Flag bits at +0x13, as the setters read and write them.
		enum flags : std::uint8_t
		{
			alpha_in = 0x01,
			alpha_out = 0x02,
			flash_word = 0x04,
			shake = 0x08,
			colour_set = 0x10,
		};

		// procedure 505 (0x465A50): set a slot up. Returns false when the index is past the table.
		bool set(std::size_t index, float x, float y, std::string text, std::uint8_t alpha);
		// procedure 506 (0x465AA0): the field setter, switching on `what` like the exe does.
		// `value` is passed as a float; the integer cases truncate it.
		bool set_field(std::size_t index, int what, float value);
		// procedure 507 (0x465BE0): write one character, the menu's typing effect. Ignored when the
		// slot has no string, exactly as the exe checks the text pointer first.
		bool put_char(std::size_t index, std::size_t at, char c);

		const slot& at(std::size_t index) const { return m_slots[index]; }
		const std::vector<slot>& all() const { return m_slots; }
		// How many slots currently hold a string.
		std::size_t used() const;

		// Drops everything, like 0x465A20 does when a level loads.
		void clear();

	private:
		std::vector<slot> m_slots = std::vector<slot>(slot_count);
	};
}
