// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#include <openrayman/game/dialog.h>
#include <openrayman/game/interpreter.h>
#include <openrayman/pointer_stream.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <memory>

namespace openrayman::game
{
	namespace
	{
		struct entry
		{
			dialog_text visible;
			std::size_t cursor = 0;
			std::uint32_t last = 0;
			int delay = 0, event = 0;
			int colour = 0;
		};
		struct dialog_state
		{
			dialog_resolver resolver;
			std::map<std::int32_t, std::string> changed;
			std::map<std::uint32_t, entry> actors;
			bool screen = false;
		};
		std::map<interpreter*, std::weak_ptr<dialog_state>> systems;

		void argument(call& c, value& v) { c.args = c.vm.eval(c.self, c.args, v); }

		std::optional<std::string> resolve(interpreter& vm, dialog_state& state, const value& v)
		{
			if(v.type == value_type::id)
			{
				auto it = state.changed.find(v.as_int());
				if(it != state.changed.end()) return it->second;
				return state.resolver ? state.resolver(v.as_int()) : std::nullopt;
			}
			if(v.type != value_type::text || !v.word()) return std::nullopt;
			std::string text;
			auto& m = vm.memory();
			for(std::uint32_t p = v.word(); m.contains(p) && text.size() < 65536; ++p)
			{
				char ch = char(m.u8(p));
				if(!ch) return text;
				text.push_back(ch);
			}
			return std::nullopt;
		}

		void store(interpreter& vm, dialog_state& state, const value& destination, const std::string& text)
		{
			if(destination.type == value_type::id)
			{
				state.changed[destination.as_int()] = text;
				return;
			}
			// The memory API does not expose destination capacity; do not overrun a text buffer.
			auto old = resolve(vm, state, destination);
			if(!old || text.size() > old->size()) return;
			auto bytes = vm.memory().span(destination.word(), text.size() + 1);
			std::copy(text.begin(), text.end(), reinterpret_cast<char*>(bytes.data()));
			bytes[text.size()] = std::byte{};
		}

		// 430 (0x470560): concatenate two resolved strings into the third destination.
		std::uint32_t concatenate(call& c, dialog_state& state)
		{
			value a, b, dest;
			argument(c, a); argument(c, b); argument(c, dest);
			auto left = resolve(c.vm, state, a), right = resolve(c.vm, state, b);
			if(left && right) store(c.vm, state, dest, *left + *right);
			return c.args;
		}

		// 243 (0x469AC0): format an integer into the resolved text destination.
		std::uint32_t format_integer(call& c, dialog_state& state)
		{
			value dest, number;
			argument(c, dest); argument(c, number);
			store(c.vm, state, dest, std::to_string(number.as_int()));
			return c.args;
		}

		// 158 (0x475680, parser 0x409060): create a generated text actor and advance its cursor.
		std::uint32_t dialog(call& c, value& out, dialog_state& state)
		{
			std::uint32_t actor_node = c.args;
			value actor, model, pos, source, timing;
			argument(c, actor); argument(c, model); argument(c, pos); argument(c, source);
			std::uint32_t timing_node = c.args;
			argument(c, timing);
			out = value::integer(0);
			auto text = resolve(c.vm, state, source);
			if(!text) return c.args;
			std::uint32_t so = actor.type == value_type::super_object ? actor.word() : c.vm.super_object_of(actor.word());
			if(!so || c.vm.removed(so))
			{
				std::uint32_t template_actor = model.type == value_type::super_object ? c.vm.actor_of(model.word()) : model.word();
				so = c.vm.generated().create(c.vm.memory(), template_actor, c.self,
					{pos.component(0), pos.component(1), pos.component(2)}, c.vm.clock());
				if(!so) return c.args;
				c.vm.restore(so);
				c.vm.assign(c.self, actor_node, value::of(value_type::actor, c.vm.actor_of(so)));
				auto& e = state.actors[so];
				e = {};
				e.visible.creator = c.self;
				e.visible.screen = state.screen;
				e.visible.x = pos.component(0); e.visible.y = pos.component(1); e.visible.z = pos.component(2);
				e.delay = std::max(0, timing.as_int());
				e.last = c.vm.clock() - std::uint32_t(e.delay);
			}
			auto it = state.actors.find(so);
			if(it == state.actors.end()) return c.args;
			auto& e = it->second;
			if(e.cursor >= text->size()) { out = value::integer(e.event); return c.args; }
			if(c.vm.clock() - e.last < std::uint32_t(std::max(0, timing.as_int()))) return c.args;
			e.last = c.vm.clock();
			if(timing.as_int() != e.delay)
				c.vm.assign(c.self, timing_node, value::integer(e.delay));
			e.event = 0;
			bool timed_control = false;
			do
			{
				while(e.cursor < text->size() && (*text)[e.cursor] == '/')
				{
					auto end = text->find(':', e.cursor + 1);
					if(end == std::string::npos) { e.cursor = text->size(); break; }
					char code = char(std::tolower(static_cast<unsigned char>((*text)[e.cursor + 1])));
					int n = std::atoi(text->c_str() + e.cursor + 2);
					e.cursor = end + 1;
					if(code == 'e')
					{
						e.event = n;
						if(std::getenv("ORTOOL_TEXT"))
							std::printf("dialog clock %u creator 0x%08X id %d event %d cursor %zu\n",
								c.vm.clock(), c.self, source.type == value_type::id ? source.as_int() : -1, n, e.cursor);
						break;
					}
					if(code == 'd' || code == 'p')
					{
						if(code == 'd') e.delay = std::max(0, n);
						c.vm.assign(c.self, timing_node, value::integer(std::max(0, n)));
						timed_control = true;
						break;
					}
					if(code == 'l') { e.visible.text.push_back('\n'); e.visible.colours.push_back(e.colour); }
					if(code == 'c') e.visible.centred = true;
					if(code == 'o') e.colour = n;
				}
				if(e.event || timed_control || e.cursor >= text->size() || (*text)[e.cursor] == '/') break;
				e.visible.text.push_back((*text)[e.cursor++]);
				e.visible.colours.push_back(e.colour);
				++e.visible.revealed;
			} while(e.delay == 0);
			out = value::integer(e.event);
			return c.args;
		}
	}

	void register_dialog_handlers(interpreter& vm)
	{
		auto state = std::make_shared<dialog_state>();
		systems[&vm] = state;
		vm.on_function(158, [state](call& c, value& out) { return dialog(c, out, *state); });
		// 443 (0x470270) selects placement for subsequent function 158 calls.
		vm.on_procedure(443, [state](call& c) { value v; argument(c, v); state->screen = v.as_int() != 0; return c.args; });
		vm.on_procedure(430, [state](call& c) { return concatenate(c, *state); });
		vm.on_procedure(243, [state](call& c) { return format_integer(c, *state); });
	}

	void set_dialog_resolver(interpreter& vm, dialog_resolver resolver)
	{
		auto it = systems.find(&vm);
		if(it != systems.end()) if(auto state = it->second.lock()) state->resolver = std::move(resolver);
	}

	dialog_resolver make_dialog_resolver(live_memory& memory, const level_files& files, unsigned language)
	{
		// Fix loader 0x402960 reads these roots from its GPT; 0x494C70 selects the table.
		pointer_stream fix(files.fix_gpt, files.fix_gpt_table, files.memory, 0);
		fix.skip_to(0xCB8);
		auto languages = fix.pointer();
		auto shared = fix.pointer();
		std::uint32_t record = languages ? languages->address() + language * 8 : 0;
		std::uint32_t general = memory.contains(record, 8) ? memory.u32(record) : 0;
		std::uint32_t count = memory.contains(record, 8) ? memory.u32(record + 4) >> 16 : 0;
		std::uint32_t common = shared && memory.contains(shared->address(), 4) ? memory.u32(shared->address()) : 0;
		return [&memory, general, common, count](std::int32_t id) -> std::optional<std::string>
		{
			if(id < 0 || (id < 20000 && std::uint32_t(id) >= count)) return std::nullopt;
			std::uint32_t index = id >= 20000 ? std::uint32_t(id - 20000) : std::uint32_t(id);
			std::uint32_t table = id >= 20000 ? common : general;
			if(index > 100000 || !table || !memory.contains(table + index * 4, 4)) return std::nullopt;
			std::uint32_t p = memory.u32(table + index * 4);
			std::string text;
			while(p && memory.contains(p) && text.size() < 65536)
			{
				char c = char(memory.u8(p++));
				if(!c) return text;
				text.push_back(c);
			}
			return std::nullopt;
		};
	}

	void set_dialog_resolver(interpreter& vm, const level_files& files, unsigned language)
	{
		set_dialog_resolver(vm, make_dialog_resolver(vm.memory(), files, language));
	}

	std::vector<dialog_text> visible_dialogs(interpreter& vm)
	{
		std::vector<dialog_text> result;
		auto it = systems.find(&vm);
		if(it == systems.end()) return result;
		auto state = it->second.lock();
		if(!state) { systems.erase(it); return result; }
		for(auto actor = state->actors.begin(); actor != state->actors.end();)
		{
			if(vm.removed(actor->first)) { actor = state->actors.erase(actor); continue; }
			// Scene directors run cue strings through function 158 too (Learn_10 node 3's
			// "/E105:/E100:..." timing lists): only events, never shown in the game. Until we know
			// what keeps them off screen there, text without a single letter isn't drawn.
			const std::string& text = actor->second.visible.text;
			if(std::ranges::any_of(text, [](char ch) { return std::isalpha(static_cast<unsigned char>(ch)) != 0; }))
				result.push_back(actor->second.visible);
			++actor;
		}
		return result;
	}
}
