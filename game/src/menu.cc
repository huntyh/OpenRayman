#include <openrayman/game/menu.h>
#include <openrayman/game/dialog.h>
#include <openrayman/game/input.h>
#include <openrayman/game/interpreter.h>
#include <openrayman/game/ui_text.h>

namespace openrayman::game
{
	void install_menu_back_routing(interpreter& vm, input& keys, std::uint32_t controller)
	{
		auto previous = vm.on_input_condition;
		vm.on_input_condition = [&vm, &keys, controller, previous](std::uint32_t actor, unsigned condition, std::uint32_t action) -> std::optional<bool> {
			if(previous) if(auto result = previous(actor, condition, action)) return result;
			if(actor != controller || condition != 45 || (action != keys.action(7) && action != keys.action(8))) return std::nullopt;
			auto& m = vm.memory();
			const auto mind = m.u32(m.u32(m.u32(controller + 4) + 0xC));
			const auto list = m.u32(m.u32(m.u32(mind)));
			const auto current = m.u32(m.u32(mind + 4) + 8);
			bool page = false;
			for(unsigned i : {3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 12u, 13u, 14u})
				if(current == list + i * 12) { page = true; break; }
			if(!page || m.read<std::int32_t>(keys.action(7) + input_action::counter) != 1) return std::nullopt;
			return action == keys.action(8);
		};
	}
	bool menu_back_entry::select(interpreter& vm, std::uint32_t controller, bool back)
	{
		if(!active || selected == back) return false;
		selected = back;
		const auto row = std::size_t(vm.get_variable(controller, 17).as_int());
		vm.screen_text().set_field(row, 7, back ? 1.0f : 3.0f);
		vm.screen_text().set_field(row, 1, back ? 0.0f : 1.0f);
		vm.set_variable(controller, 53, value::integer(500));
		vm.set_variable(controller, 54, value::integer(int(vm.screen_text().at(back ? slot : row).y)));
		return true;
	}
	bool menu_back_entry::update(interpreter& vm, input& keys, std::uint32_t controller)
	{
		auto& m = vm.memory();
		const auto mind = m.u32(m.u32(m.u32(controller + 4) + 0xC));
		const auto list = m.u32(m.u32(m.u32(mind)));
		const auto current = m.u32(m.u32(mind + 4) + 8);
		const auto reflex = m.u32(m.u32(mind + 8) + 8);
		const auto reflexes = m.u32(m.u32(m.u32(mind) + 4));
		// 7: save-slot list; 8: name; 10: load; 13/14: delete variants. Other
		// pages either have their own Back or cannot be left (first language choice).
		const bool page = current == list + 7 * 12 || current == list + 8 * 12 || current == list + 10 * 12 || current == list + 13 * 12 || current == list + 14 * 12;
		const bool ready = reflex == reflexes || reflex == reflexes + 2 * 12 || reflex == reflexes + 6 * 12;
		const bool up = keys.key(0xC8) || keys.pad_axis(0, 5) < -16384;
		const bool down = keys.key(0xD0) || keys.pad_axis(0, 5) > 16384;
		const bool up_pressed = up && !up_down, down_pressed = down && !down_down;
		up_down = up; down_down = down;
		if(current != behaviour) { selected = false; behaviour = current; }
		active = page && ready;
		const bool naming = active && menu_name_screen(vm, controller);
		if(!naming && hint_visible) { vm.screen_text().set_field(6, 0, 0); hint_visible = false; }
		if(!active) { selected = false; vm.screen_text().set_field(slot, 0, 0); return false; }
		const int row = vm.get_variable(controller, 17).as_int();
		bool skip = false;
		if(selected) {
			if(keys.key(1)) {
				select(vm, controller, false);
				return false; // deliver the physical Esc press on this tick
			}
			if(keys.key(0x1C) || keys.key(0x39) || keys.pad_button(0, 10)) {
				select(vm, controller, false);
				// Next input tick delivers exactly the Esc action used by this page.
				keys.pulse_key(1);
				skip = true;
			} else if(up_pressed || down_pressed) {
				select(vm, controller, false);
				if(!naming) {
					int target = -1;
					for(int i = 0; i < vm.get_variable(controller, 21).as_int(); ++i)
						if(script_menu_entry(vm, controller, i, false)) { target = i; if(down_pressed) break; }
					if(target >= 0) select_script_menu_entry(vm, controller, target, false);
				}
				vm.play_sound(menu_move_sound, 0, 119, -1); skip = true;
			} else skip = true;
		} else if(up_pressed || down_pressed) {
			bool last = true, first = true;
			for(int i = 0; i < vm.get_variable(controller, 21).as_int(); ++i)
				if(script_menu_entry(vm, controller, i, false)) { if(i > row) last = false; if(i < row) first = false; }
			if((down_pressed && (naming || last)) || (up_pressed && !naming && first)) {
				select(vm, controller, true); vm.play_sound(menu_move_sound, 0, 119, -1); skip = true;
			}
		}
		vm.screen_text().set(slot, 500, naming ? 920.0f : 850.0f, "/C:" + ui_text(ui_key::back, vm.language), 255,
			vm.get_variable(controller, 2).as_float(), controller);
		vm.screen_text().set_field(slot, 7, selected ? 3.0f : 1.0f);
		vm.screen_text().set_field(slot, 1, selected ? 1.0f : 0.0f);
		if(naming) {
			const auto old = vm.screen_text().at(8);
			if(old.used) {
				const auto hint = ui_text(ui_key::name_hint, vm.language);
				const auto split = hint.find("/L:");
				vm.screen_text().set(8, old.x, old.y, "/C:" + hint.substr(0,split), old.alpha, 12, controller);
				vm.screen_text().set(6, old.x, old.y + 60, "/C:" + hint.substr(split+3), old.alpha, 12, controller);
				hint_visible = true;
			}
		}
		return skip;
	}
}
