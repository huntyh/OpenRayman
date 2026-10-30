#include <openrayman/game/screen_text.h>

#include <cmath>

namespace openrayman::game
{
	// The exe's out-of-range behaviour is to do nothing at all, so every setter checks 0x32 first.
	static bool in_range(std::size_t index)
	{
		return index < screen_text::slot_count;
	}

	bool screen_text::set(std::size_t index, float x, float y, std::string text, std::uint8_t alpha)
	{
		if(!in_range(index))
			return false;
		slot& s = m_slots[index];
		s.used = true;
		s.text = std::move(text);
		s.x = x;
		s.y = y;
		s.size = 0;
		s.alpha = alpha;
		s.wobble = 0;
		s.flags = 0;
		s.word = 0;
		s.colour = 0;
		return true;
	}

	bool screen_text::set_field(std::size_t index, int what, float value)
	{
		if(!in_range(index))
			return false;
		slot& s = m_slots[index];
		switch(what)
		{
		case 0:
			// The exe stores this straight into the byte at +0x10.
			s.alpha = std::uint8_t(value);
			return true;
		case 1:
			// A boolean into the byte at +0x12, the wobble flag.
			s.wobble = value != 0 ? 1 : 0;
			return true;
		case 2:
			// Whole-byte mode writes, not bits: the exe assigns 1 and 2 straight to +0x13.
			s.flags = 1;
			if(s.alpha == 0)
				s.alpha = 1;
			return true;
		case 3:
			s.flags = 2;
			return true;
		case 4:
			s.size = value;
			return true;
		case 5:
			if(value == 0)
			{
				s.flags &= std::uint8_t(~flash_word);
			}
			else
			{
				s.word = std::uint8_t(value);
				s.flags |= flash_word;
			}
			return true;
		case 6:
			// The exe compares against 1.0 exactly, so any other value clears the bit.
			if(value == 1.0f)
				s.flags |= shake;
			else
				s.flags &= std::uint8_t(~shake);
			return true;
		case 7:
			s.colour = std::uint8_t(value);
			if(value == 0)
				s.flags &= std::uint8_t(~colour_set);
			else
				s.flags |= colour_set;
			return true;
		default:
			return false;
		}
	}

	bool screen_text::put_char(std::size_t index, std::size_t at, char c)
	{
		if(!in_range(index))
			return false;
		slot& s = m_slots[index];
		if(!s.used)
			return false;
		// The exe writes at a raw offset into the engine's buffer and grows nothing.
		if(at >= s.text.size())
			s.text.resize(at + 1, '\0');
		s.text[at] = c;
		return true;
	}

	std::size_t screen_text::used() const
	{
		std::size_t count = 0;
		for(const slot& s : m_slots)
			if(s.used)
				count++;
		return count;
	}

	void screen_text::clear()
	{
		for(slot& s : m_slots)
			s = slot{};
	}
}
