#include <openrayman/game/input.h>

#include <format>
#include <limits>
#include <stdexcept>

namespace openrayman::game
{
	namespace
	{
		constexpr std::uint32_t slot = 8;
	}

	input::input(live_memory& memory, const input_actions& actions)
		: m_memory(memory), m_first(live_memory::address(actions.first)), m_count(actions.count)
	{
	}

	void input::set_key(std::uint8_t scan_code, bool down)
	{
		m_keys[scan_code] = down;
	}

	void input::release_all()
	{
		m_keys.fill(false);
	}

	std::uint32_t input::action(std::size_t index) const
	{
		return index < m_count ? m_first + std::uint32_t(index * input_action_size) : 0;
	}

	bool input::evaluate(std::uint32_t& at, std::uint32_t end)
	{
		// The poll functions of table 0x4A0F18; each moves the cursor past its slots. Logic nodes
		// evaluate both sides (no short cut), as the exe does.
		if(at + slot > end)
			return false;
		std::uint8_t kind = m_memory.u8(at);
		auto size = input_node_slots(kind);
		if(!size)
		{
			at = end; // a kind we can't step over: the rest of the expression is lost
			return false;
		}
		std::uint32_t node = at;
		at += std::uint32_t(*size * slot);
		switch(input_kind(kind))
		{
		case input_kind::all: { bool a = evaluate(at, end); bool b = evaluate(at, end); return a && b; }
		case input_kind::any: { bool a = evaluate(at, end); bool b = evaluate(at, end); return a || b; }
		case input_kind::not_: return !evaluate(at, end);
		case input_kind::key_pressed:
		case input_kind::key_released:
		case input_kind::key_down:
		case input_kind::key_up:
		{
			// 0x42EC10..0x42ED50: operand slot = i16 scan code, then at +5/+6 the range of ticks
			// since the key last changed (0x509EC0, kept by 0x430880).
			std::uint8_t key = std::uint8_t(m_memory.read<std::int16_t>(node + slot));
			std::uint8_t lo = m_memory.u8(node + slot + 5), hi = m_memory.u8(node + slot + 6);
			bool in_range = m_frames[key] >= lo && m_frames[key] <= hi;
			switch(input_kind(kind))
			{
			case input_kind::key_pressed: return m_now[key] && !m_before[key];
			case input_kind::key_released: return !m_now[key] && m_before[key];
			case input_kind::key_down: return m_now[key] && in_range;
			default: return !m_now[key] && in_range;
			}
		}
		case input_kind::action_pressed:
		case input_kind::action_released:
		case input_kind::action_held:
		case input_kind::action_free:
		{
			// 0x430130..0x4301C0: another action's counter, through 0x42E850/890/870/8B0.
			std::uint32_t other = m_memory.u32(node + slot);
			if(!other)
				return false;
			std::int32_t counter = m_memory.read<std::int32_t>(other + input_action::counter);
			switch(input_kind(kind))
			{
			case input_kind::action_pressed: return counter == 1;
			case input_kind::action_released: return counter == -1;
			case input_kind::action_held: return counter > 0;
			default: return counter < 0;
			}
		}
		default:
			return false; // joysticks, typed sequences
		}
	}

	void input::step()
	{
		// 0x430880: ticks since each key changed, reset on the change, counted up to 255.
		m_before = m_now;
		m_now = m_keys;
		for(std::size_t k = 0; k < 256; k++)
		{
			if(m_now[k] != m_before[k])
				m_frames[k] = 0;
			if(m_frames[k] < 255)
				m_frames[k]++;
		}
		// 0x42E710: counter > 0 = ticks held (1 on the press tick), < 0 = ticks since release (-1 on
		// the release tick). A counter of exactly 0 never moves; the data starts them at -2.
		for(std::size_t i = 0; i < m_count; i++)
		{
			std::uint32_t a = action(i);
			if(m_memory.u8(a + input_action::enabled) != 1)
				continue;
			m_memory.write<float>(a + input_action::analog, 0.0f);
			std::uint32_t expression = m_memory.u32(a + input_action::expression);
			std::uint32_t at = expression;
			bool down = false; // no expression: up
			try
			{
				down = expression && evaluate(at, expression + m_memory.u32(a + input_action::slots) * slot);
			}
			catch(const std::exception& e)
			{
				throw std::runtime_error(std::format("input action {} (expression 0x{:08X}, slot 0x{:08X}): {}", i, expression, at, e.what()));
			}
			std::int32_t counter = m_memory.read<std::int32_t>(a + input_action::counter);
			if(down && counter > 0)
				counter = counter == std::numeric_limits<std::int32_t>::max() ? counter : counter + 1;
			else if(!down && counter < 0)
				counter = counter == std::numeric_limits<std::int32_t>::min() ? counter : counter - 1;
			else if(!down && counter > 0)
				counter = -1;
			else if(down && counter < 0)
				counter = 1;
			m_memory.write<std::int32_t>(a + input_action::counter, counter);
		}
	}
}
