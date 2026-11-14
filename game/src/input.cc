#include <openrayman/game/input.h>

#include <algorithm>
#include <format>
#include <limits>
#include <stdexcept>

namespace openrayman::game
{
	namespace
	{
		constexpr std::uint32_t slot = 8;

		// The game reads a pad's axes through its capabilities and scales them by this, so a fully
		// pushed stick reads 100 (0x42F325; docs/formats/input.md).
		constexpr float axis_scale = 100.0f;
	}

	input::input(live_memory& memory, const input_actions& actions)
		: m_memory(memory), m_first(live_memory::address(actions.first)), m_count(actions.count)
	{
	}

	void input::set_key(std::uint8_t scan_code, bool down)
	{
		m_keys[scan_code] = down;
	}

	void input::set_pad_button(std::size_t pad, std::size_t button, bool down)
	{
		if(pad < pad_count && button < pad_button_count)
			m_pad_buttons[pad][button] = down;
	}

	void input::set_pad_axis(std::size_t pad, std::size_t axis, std::int32_t value)
	{
		if(pad < pad_count && axis >= pad_first_axis && axis < pad_first_axis + pad_axis_count)
			m_pad_axes[pad][axis - pad_first_axis] = std::clamp(value, -32768, 32767);
	}

	void input::release_all()
	{
		m_keys.fill(false);
		for(std::size_t pad = 0; pad < pad_count; pad++)
			release_pad(pad);
	}

	void input::release_pad(std::size_t pad)
	{
		if(pad >= pad_count)
			return;
		m_pad_buttons[pad].fill(false);
		m_pad_axes[pad].fill(0);
	}

	bool input::pad_button(std::size_t pad, std::size_t button) const
	{
		return pad < pad_count && button < pad_button_count && m_pad_now[pad][button];
	}

	std::int32_t input::pad_axis(std::size_t pad, std::size_t axis) const
	{
		if(pad >= pad_count || axis < pad_first_axis || axis >= pad_first_axis + pad_axis_count)
			return 0;
		return m_pad_axes[pad][axis - pad_first_axis];
	}

	std::uint32_t input::action(std::size_t index) const
	{
		return index < m_count ? m_first + std::uint32_t(index * input_action_size) : 0;
	}

	bool input::pad_button_test(std::uint8_t kind, std::int16_t pad_number, std::int16_t button, std::uint8_t lo, std::uint8_t hi) const
	{
		if(pad_number < 0 || std::size_t(pad_number) >= pad_count || button < 0 || std::size_t(button) >= pad_button_count)
			return false;
		std::size_t p = std::size_t(pad_number), b = std::size_t(button);
		bool now = m_pad_now[p][b], before = m_pad_before[p][b];
		bool in_range = m_pad_frames[p][b] >= lo && m_pad_frames[p][b] <= hi;
		switch(input_kind(kind))
		{
		case input_kind::pad_pressed: return now && !before;
		case input_kind::pad_released: return !now && before;
		case input_kind::pad_down: return now && in_range;
		case input_kind::pad_up: return !now && in_range;
		// 26 is the analog pads' test in the exe; the game's data uses it for every button it names
		// (jump, shoot, camera), so it is the held test for the pads we open.
		default: return now && in_range;
		}
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
		case input_kind::pad_pressed:
		case input_kind::pad_released:
		case input_kind::pad_down:
		case input_kind::pad_up:
		case input_kind::pad_held:
		{
			// 0x42EDB0..0x42F120 and 0x42FE80: operand slot = i16 pad, the next = i16 button with,
			// for the timed kinds, the range of ticks since it last changed at its bytes +5/+6
			// (node +0x15/+0x16, 0x42EFE0).
			std::int16_t pad_number = m_memory.read<std::int16_t>(node + slot);
			std::int16_t button = m_memory.read<std::int16_t>(node + slot * 2);
			std::uint8_t lo = m_memory.u8(node + slot * 2 + 5), hi = m_memory.u8(node + slot * 2 + 6);
			return pad_button_test(kind, pad_number, button, lo, hi);
		}
		case input_kind::pad_axis:
		{
			// 0x42F270: operand slots = pad, axis, min, max. The axis's position over the pad's
			// range, times 100, is the value: compared with min..max, and (when it fits) kept as
			// the action's analog value at +0x10, which step() clears every tick.
			std::int16_t pad_number = m_memory.read<std::int16_t>(node + slot);
			std::int16_t axis = m_memory.read<std::int16_t>(node + slot * 2);
			std::int16_t min = m_memory.read<std::int16_t>(node + slot * 3);
			std::int16_t max = m_memory.read<std::int16_t>(node + slot * 4);
			if(!m_analog_action)
				return false;
			float value = 0;
			if(pad_number >= 0 && std::size_t(pad_number) < pad_count &&
				axis >= std::int16_t(pad_first_axis) && axis < std::int16_t(pad_first_axis + pad_axis_count))
				value = float(pad_axis(std::size_t(pad_number), std::size_t(axis))) / 32768.0f * axis_scale;
			int rounded = int(value); // ftol: towards zero
			if(rounded < min || rounded > max)
				return false;
			m_memory.write<float>(m_analog_action + input_action::analog, value);
			return true;
		}
		default:
			return false; // typed sequences (33..37)
		}
	}

	void input::step()
	{
		// 0x430880: ticks since each key changed, reset on the change, counted up to 255. The pads
		// keep the same per-button table.
		m_before = m_now;
		m_now = m_keys;
		for(std::size_t k = 0; k < 256; k++)
		{
			if(m_now[k] != m_before[k])
				m_frames[k] = 0;
			if(m_frames[k] < 255)
				m_frames[k]++;
		}
		for(std::size_t pad = 0; pad < pad_count; pad++)
		{
			m_pad_before[pad] = m_pad_now[pad];
			m_pad_now[pad] = m_pad_buttons[pad];
			for(std::size_t b = 0; b < pad_button_count; b++)
			{
				if(m_pad_now[pad][b] != m_pad_before[pad][b])
					m_pad_frames[pad][b] = 0;
				if(m_pad_frames[pad][b] < 255)
					m_pad_frames[pad][b]++;
			}
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
			m_analog_action = a;
			try
			{
				down = expression && evaluate(at, expression + m_memory.u32(a + input_action::slots) * slot);
			}
			catch(const std::exception& e)
			{
				m_analog_action = 0;
				throw std::runtime_error(std::format("input action {} (expression 0x{:08X}, slot 0x{:08X}): {}", i, expression, at, e.what()));
			}
			m_analog_action = 0;
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
