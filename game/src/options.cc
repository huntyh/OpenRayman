#include <openrayman/game/options.h>
#include <algorithm>
#include <charconv>
#include <fstream>

namespace openrayman::game
{
	namespace
	{
		struct setting { const char* name; int engine_options::*member; int low, high; };
		constexpr setting integers[] = {{"music", &engine_options::music, 0, 10}, {"effects", &engine_options::effects, 0, 10},
			{"vsync", &engine_options::vsync, 0, 2}, {"fps_limit", &engine_options::fps, 0, 1000}, {"language", &engine_options::language, -1, 4}};
		constexpr option_entry root[] = {{option_id::sound,"Sound"}, {option_id::display,"Display"}, {option_id::game,"Game"}, {option_id::back,"Back"}};
		constexpr option_entry sound[] = {{option_id::music,"Music volume"}, {option_id::effects,"Effects volume"}, {option_id::back,"Back"}};
		constexpr option_entry display[] = {{option_id::fullscreen,"Fullscreen"}, {option_id::vsync,"Vsync"}, {option_id::fps,"FPS limit"}, {option_id::loading,"Loading screens"}, {option_id::back,"Back"}};
		constexpr option_entry game[] = {{option_id::language,"Language"}, {option_id::autosave,"Autosave"}, {option_id::back,"Back"}};
		constexpr const char* languages[] = {"English", "Fran\xE7" "ais", "Espa\xF1ol", "Italiano", "Deutsch"};
	}
	engine_options read_options(const std::filesystem::path& root)
	{
		engine_options result;
		std::ifstream in(root / "openrayman.cfg");
		for(std::string line; std::getline(in, line);)
		{
			auto equals = line.find('=');
			if(equals == std::string::npos) continue;
			auto key = line.substr(0, equals), number = line.substr(equals + 1);
			if(!number.empty() && number.back() == '\r') number.pop_back();
			int value = 0;
			auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
			if(parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) continue;
			for(auto s : integers) if(key == s.name && value >= s.low && value <= s.high) result.*(s.member) = value;
			if(value != 0 && value != 1) continue;
			if(key == "fullscreen") result.fullscreen = value != 0;
			if(key == "loading_screens") result.loading_screens = value != 0;
		}
		return result;
	}
	bool write_options(const std::filesystem::path& root, const engine_options& values)
	{
		std::error_code ec;
		std::filesystem::create_directories(root, ec);
		if(ec) return false;
		const auto temporary = root / "openrayman.cfg.tmp";
		std::ofstream out(temporary, std::ios::trunc);
		for(auto s : integers) out << s.name << '=' << values.*(s.member) << '\n';
		out << "fullscreen=" << values.fullscreen << "\nloading_screens=" << values.loading_screens << '\n';
		out.close();
		if(!out) return false;
		// copy_file replaces the old file only after the complete staging file was closed.
		std::filesystem::copy_file(temporary, root / "openrayman.cfg", std::filesystem::copy_options::overwrite_existing, ec);
		if(ec) return false;
		std::filesystem::remove(temporary, ec);
		return true;
	}
	std::span<const option_entry> options_page::entries() const
	{
		switch(current) { case page::sound: return sound; case page::display: return display; case page::game: return game; default: return root; }
	}
	bool options_page::navigate(action input, engine_options& v, bool& autosave, bool has_slot)
	{
		const int count = int(entries().size());
		if(input == action::up) { selected = (selected + count - 1) % count; return false; }
		if(input == action::down) { selected = (selected + 1) % count; return false; }
		auto id = entries()[std::size_t(selected)].id;
		if(input == action::back || (input == action::confirm && id == option_id::back))
		{
			if(current == page::root) active = false;
			else { selected = current == page::sound ? 0 : current == page::display ? 1 : 2; current = page::root; }
			return false;
		}
		if(input == action::confirm)
		{
			if(id == option_id::sound) current = page::sound;
			else if(id == option_id::display) current = page::display;
			else if(id == option_id::game) current = page::game;
			else return false;
			selected = 0; return false;
		}
		if(input != action::left && input != action::right) return false;
		const int delta = input == action::left ? -1 : 1;
		auto before = v;
		switch(id)
		{
		case option_id::music: v.music = std::clamp(v.music + delta, 0, 10); break;
		case option_id::effects: v.effects = std::clamp(v.effects + delta, 0, 10); break;
		case option_id::fullscreen: v.fullscreen = !v.fullscreen; break;
		case option_id::vsync: v.vsync = (v.vsync + delta + 3) % 3; break;
		case option_id::fps:
		{
			constexpr int rates[] = {0, 30, 60, 90, 120, 144, 165, 240};
			auto at = std::lower_bound(std::begin(rates), std::end(rates), v.fps);
			int index = at == std::end(rates) ? 7 : int(at - std::begin(rates));
			v.fps = rates[(index + delta + 8) % 8]; break;
		}
		case option_id::loading: v.loading_screens = !v.loading_screens; break;
		case option_id::language: v.language = (std::max(v.language, 0) + delta + 5) % 5; break;
		case option_id::autosave: if(has_slot) { autosave = !autosave; return true; } break;
		default: break;
		}
		return !(before == v);
	}
	void options_page::draw(screen_text& slots, const engine_options& v, bool autosave, unsigned slot) const
	{
		slots.clear();
		const char* title = current == page::sound ? "Options - Sound" : current == page::display ? "Options - Display" : current == page::game ? "Options - Game" : "Options";
		slots.set(0, 500, 100, std::string("/C:") + title, 255, 20);
		int row = 0;
		for(auto entry : entries())
		{
			std::string value;
			switch(entry.id)
			{
			case option_id::music: value = std::to_string(v.music * 10) + "%"; break;
			case option_id::effects: value = std::to_string(v.effects * 10) + "%"; break;
			case option_id::fullscreen: value = v.fullscreen ? "On" : "Off"; break;
			case option_id::vsync: value = v.vsync == 0 ? "On" : v.vsync == 1 ? "Strict" : "Off"; break;
			case option_id::fps: value = v.fps ? std::to_string(v.fps) : "Off"; break;
			case option_id::loading: value = v.loading_screens ? "On" : "Off"; break;
			case option_id::language: value = languages[std::size_t(std::clamp(v.language, 0, 4))]; break;
			case option_id::autosave: value = slot ? (autosave ? "On" : "Off") : "No active save"; break;
			default: break;
			}
			const auto index = std::size_t(++row);
			slots.set(index, 500, 350.0f + float(row - 1) * 100, "/C:" + std::string(entry.label) + (value.empty() ? "" : "  " + value), 255, 14);
			slots.set_field(index, 7, row - 1 == selected ? 3.0f : 1.0f);
			slots.set_field(index, 1, row - 1 == selected ? 1.0f : 0.0f);
		}
		slots.set(10, 500, 900, "/C:Arrows move change   Enter select   Esc back", 255, 7);
		if(!error.empty()) slots.set(11, 500, 920, "/C:/O200:" + error, 255, 8);
	}
}

