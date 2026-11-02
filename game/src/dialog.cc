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
	std::optional<int> dialog_glyph_index(std::uint8_t byte, int offset)
	{
		if(byte < 0x20) return std::nullopt;
		int base = byte - ((byte >= 0x61 && byte <= 0x7a) || byte > 0x9f ? 0x40 : 0x20);
		return base + offset;
	}

	dialog_layout layout_dialog(const dialog_text& dialog)
	{
		dialog_layout result;
		float x = 0, y = 0, height = dialog.line_height, width = 0, scale = dialog.default_scale;
		int offset = 0, count = 0;
		const auto& text = dialog.prepared;
		for(std::size_t i = 0; i < text.size();)
		{
			if(text[i] == '/')
			{
				auto end = text.find(':', i + 1);
				if(end == std::string::npos || i + 1 >= text.size()) break;
				char code = char(std::tolower(static_cast<unsigned char>(text[i + 1])));
				int n = std::atoi(text.c_str() + i + 2);
				switch(code)
				{
				case 'h': height = n * 0.001f; break;
				case 'w': width = n * 0.001f; break;
				case 'z': scale = n * 0.001f; break;
				case 'o': offset = n; break;
				case 'c': result.centred = true; break;
				case 'l': result.width = std::max(result.width, x); x = 0; y += height; break;
				}
				i = end + 1;
				continue;
			}
			auto byte = static_cast<std::uint8_t>(text[i++]);
			auto base = dialog_glyph_index(byte);
			float size = scale != 0 ? scale : dialog.scales.empty() ? 1.0f : dialog.scales[std::size_t(count) % dialog.scales.size()];
			if(base)
			{
				float advance = width != 0 ? width : dialog.widths[std::size_t(*base)];
				result.glyphs.push_back({*base + offset, byte, x, y, size, advance * size, count < dialog.revealed});
				x += advance * size;
			}
			++count;
		}
		result.width = std::max(result.width, x);
		return result;
	}

	namespace
	{
		struct entry
		{
			dialog_text visible;
			std::size_t cursor = 0;
			std::uint32_t last = 0;
			int delay = 0, event = 0;
		};
		struct dialog_state
		{
			dialog_state() { widths.fill(1.0f); }
			dialog_resolver resolver;
			std::map<std::int32_t, std::string> changed;
			std::map<std::uint32_t, entry> actors;
			// Initial value of the placement byte at 0x4A40B4 in Rayman2.exe.
			bool screen = true;
			std::array<float, 256> widths;
			std::vector<float> scales;
			float line_height = 2, default_scale = 0;
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
			// 0x475680: these special strings configure global tables and create no actor.
			if(text->size() >= 3 && (*text)[0] == '/' && (*text)[1] == '-')
			{
				char kind = char(std::tolower(static_cast<unsigned char>((*text)[2])));
				if(kind == 'w' || kind == 'z')
				{
					std::vector<float> values;
					const char* p = text->c_str() + 3;
					while(*p)
					{
						char* end = nullptr;
						long n = std::strtol(p, &end, 10);
						if(end == p) { ++p; continue; }
						values.push_back(float(n) * 0.001f);
						p = end;
					}
					if(kind == 'w') for(std::size_t i = 0; i < values.size() && i < state.widths.size(); ++i) state.widths[i] = values[i];
					else state.scales = std::move(values);
					return c.args;
				}
			}
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
				e.visible.template_actor = template_actor;
				e.visible.widths = state.widths;
				e.visible.scales = state.scales;
				e.visible.line_height = state.line_height;
				e.visible.default_scale = state.default_scale;
				e.visible.screen = state.screen;
				e.visible.x = pos.component(0); e.visible.y = pos.component(1); e.visible.z = pos.component(2);
				e.delay = std::max(0, timing.as_int());
				e.last = c.vm.clock() - std::uint32_t(e.delay);
			}
			auto it = state.actors.find(so);
			if(it == state.actors.end()) return c.args;
			auto& e = it->second;
			e.visible.prepared = *text;
			e.visible.widths = state.widths;
			e.visible.scales = state.scales;
			e.visible.line_height = state.line_height;
			e.visible.default_scale = state.default_scale;
			if(e.cursor >= text->size()) { out = value::integer(e.event); return c.args; }
			// Inside the delay the cursor waits, except right after an event: then the next control is
			// read at once without restarting the delay (0x475680), so back-to-back cues (a camera cut,
			// then the actors it shows) land on consecutive frames, not a delay apart.
			if(c.vm.clock() - e.last < std::uint32_t(std::max(0, timing.as_int())))
			{
				if(!e.event) return c.args;
			}
			else
			{
				e.last = c.vm.clock();
				if(timing.as_int() != e.delay)
					c.vm.assign(c.self, timing_node, value::integer(e.delay));
			}
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
					if(code == 'l') e.visible.text.push_back('\n');
				}
				if(e.event || timed_control || e.cursor >= text->size() || (*text)[e.cursor] == '/') break;
				e.visible.text.push_back((*text)[e.cursor++]);
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
		// 352 (0x46FD70 / 0x408590): reset the glyph widths and select default
		// line height and size. Negative height or size leaves that default alone.
		vm.on_procedure(352, [state](call& c) {
			value width, height, scale;
			argument(c, width); argument(c, height); argument(c, scale);
			state->widths.fill(width.as_float());
			if(height.as_float() >= 0) state->line_height = height.as_float();
			if(scale.as_float() >= 0) state->default_scale = scale.as_float();
			return c.args;
		});
		vm.on_procedure(430, [state](call& c) { return concatenate(c, *state); });
		vm.on_procedure(243, [state](call& c) { return format_integer(c, *state); });
	}

	void set_dialog_resolver(interpreter& vm, dialog_resolver resolver)
	{
		auto it = systems.find(&vm);
		if(it != systems.end()) if(auto state = it->second.lock()) state->resolver = std::move(resolver);
	}

	std::optional<std::string> resolve_dialog_value(interpreter& vm, const value& v)
	{
		auto it = systems.find(&vm);
		if(it != systems.end()) if(auto state = it->second.lock()) return resolve(vm, *state, v);
		return std::nullopt;
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
			// Cue strings can contain only controls; they have no characters to draw.
			const std::string& text = actor->second.visible.text;
			if(!text.empty())
				result.push_back(actor->second.visible);
			++actor;
		}
		return result;
	}
}
