// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/input.h>
#include <openrayman/error.h>
#include <openrayman/pointer_stream.h>

#include <format>

namespace openrayman
{
	namespace
	{
		// Fix.gpt: after a pointer at 0x184 the game copies 0xB20 bytes of input state; the action
		// count and the array pointer sit 0x700 into them (Rayman2.exe 0x402D0F..0x402D2E, the
		// count and array globals 0x50A560/0x50A564).
		constexpr std::size_t fix_input_count = 0x888;
		constexpr std::uint32_t slot_size = 8;

		const char* const key_names[256] = {
			nullptr, "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace", "Tab",
			"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "LCtrl", "A", "S",
			"D", "F", "G", "H", "J", "K", "L", ";", "'", "`", "LShift", "\\", "Z", "X", "C", "V",
			"B", "N", "M", ",", ".", "/", "RShift", "Num*", "LAlt", "Space", "CapsLock", "F1", "F2", "F3", "F4", "F5",
			"F6", "F7", "F8", "F9", "F10", "NumLock", "ScrollLock", "Num7", "Num8", "Num9", "Num-", "Num4", "Num5", "Num6", "Num+", "Num1",
			"Num2", "Num3", "Num0", "Num.", nullptr, nullptr, nullptr, "F11", "F12",
		};

		struct describer
		{
			const memory_image& memory;
			const input_actions& actions;
			memory_location at;
			std::size_t left;
			bool stopped = false;

			std::uint8_t byte(std::uint32_t offset) const
			{
				if(std::size_t(at.offset) + offset >= at.block->data.size())
					throw format_error("input expression runs past its block");
				return std::uint8_t(at.block->data[at.offset + offset]);
			}

			std::int16_t word(std::uint32_t slot) const
			{
				return std::int16_t(byte(slot * slot_size) | byte(slot * slot_size + 1) << 8);
			}

			std::string action_name(std::uint32_t slot) const
			{
				auto to = memory.follow({ at.block, at.offset + slot * slot_size });
				if(!to)
					return "action ?";
				if(to->block == actions.first.block && to->offset >= actions.first.offset && (to->offset - actions.first.offset) % input_action_size == 0)
					return std::format("action {}", (to->offset - actions.first.offset) / input_action_size);
				return std::format("action at {} +0x{:X}", to->block->id.to_string(), to->offset);
			}

			std::string node()
			{
				if(left == 0)
					return "(missing)";
				std::uint8_t kind = byte(0);
				auto size = input_node_slots(kind);
				if(!size || *size > left)
				{
					stopped = true;
					return std::format("kind {}", kind);
				}
				std::string text;
				std::string range = std::format("{}..{} frames", byte(slot_size + 5), byte(slot_size + 6));
				switch(input_kind(kind))
				{
					case input_kind::key_pressed: text = "pressed " + key_name(std::uint16_t(word(1))); break;
					case input_kind::key_released: text = "released " + key_name(std::uint16_t(word(1))); break;
					case input_kind::key_down: text = "key " + key_name(std::uint16_t(word(1))); break;
					case input_kind::key_up: text = "not key " + key_name(std::uint16_t(word(1))); break;
					case input_kind::action_pressed: text = "pressed " + action_name(1); break;
					case input_kind::action_released: text = "released " + action_name(1); break;
					case input_kind::action_held: text = "held " + action_name(1); break;
					case input_kind::action_free: text = "free " + action_name(1); break;
					case input_kind::pad_axis:
						text = std::format("pad {} axis {} in {}..{}", word(1), word(2), word(3), word(4));
						break;
					case input_kind::sequence_key: text = "down " + key_name(std::uint16_t(word(1))); break;
					case input_kind::sequence_release: text = "up " + key_name(std::uint16_t(word(1))); break;
					default:
						if((kind >= 12 && kind <= 15) || kind >= 26)
							text = std::format("pad {} button {} (kind {})", word(1), word(2), kind);
						break;
				}
				if((kind == 6 || kind == 7) && (byte(slot_size + 5) != 0 || byte(slot_size + 6) != 0xFF))
					text += " for " + range;
				std::uint32_t steps = kind == std::uint8_t(input_kind::sequence) ? std::uint32_t(byte(slot_size) | byte(slot_size + 1) << 8) : 0;
				at.offset += std::uint32_t(*size * slot_size);
				left -= *size;
				if(kind == std::uint8_t(input_kind::sequence))
				{
					std::string list;
					for(std::uint32_t i = 0; i < steps && !stopped; i++)
						list += (i ? ", " : "") + node();
					return "typed(" + list + ")";
				}
				if(kind == std::uint8_t(input_kind::not_))
					return "not(" + node() + ")";
				if(kind == std::uint8_t(input_kind::all) || kind == std::uint8_t(input_kind::any))
				{
					std::string a = node();
					if(stopped)
						return (kind == 1 ? "all(" : "any(") + a + ", ...)";
					std::string b = node();
					return (kind == 1 ? "all(" : "any(") + a + ", " + b + ")";
				}
				return text;
			}
		};
	}

	input_actions read_input_actions(const level_files& files)
	{
		pointer_stream fix(files.fix_gpt, files.fix_gpt_table, files.memory, 0);
		fix.skip_to(fix_input_count);
		input_actions actions;
		actions.count = fix.u32();
		actions.first = fix.required_pointer();
		if(std::size_t(actions.first.offset) + actions.count * input_action_size > actions.first.block->data.size())
			throw format_error("input actions run past their block");
		return actions;
	}

	std::optional<std::size_t> input_node_slots(std::uint8_t kind)
	{
		// From the exe's poll functions (table 0x4A0F18): how far each moves the read cursor.
		if(kind >= 1 && kind <= 3)
			return 1;
		if(kind >= 4 && kind <= 11)
			return 2;
		if((kind >= 12 && kind <= 15) || kind == 26 || kind == 36 || kind == 37)
			return 3;
		if(kind == 16)
			return 5;
		if(kind == 33 || kind == 34 || kind == 35)
			return 2;
		return std::nullopt;
	}

	std::string describe_input_action(const memory_image& memory, const input_actions& actions, const memory_location& action)
	{
		std::uint32_t slots = memory.read_u32(action);
		auto expression = memory.follow({ action.block, action.offset + input_action::expression });
		if(!expression)
			return "(none)";
		describer d{ memory, actions, *expression, slots };
		std::string text = d.node();
		if(!d.stopped && d.left != 0)
			text += std::format(" (+{} slots)", d.left);
		return text;
	}

	std::string key_name(std::uint16_t scan_code)
	{
		switch(scan_code)
		{
			case 0x9C: return "NumEnter";
			case 0x9D: return "RCtrl";
			case 0xB8: return "RAlt";
			case 0xC7: return "Home";
			case 0xC8: return "Up";
			case 0xC9: return "PgUp";
			case 0xCB: return "Left";
			case 0xCD: return "Right";
			case 0xCF: return "End";
			case 0xD0: return "Down";
			case 0xD1: return "PgDn";
			case 0xD2: return "Insert";
			case 0xD3: return "Delete";
		}
		if(scan_code < 256 && key_names[scan_code])
			return key_names[scan_code];
		return std::format("0x{:02X}", scan_code);
	}
}
