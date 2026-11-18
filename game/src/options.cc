#include <openrayman/game/options.h>
#include <openrayman/game/ui_text.h>
#include <algorithm>
#include <charconv>
#include <fstream>

namespace openrayman::game
{
	namespace
	{
		struct setting { const char* name; int engine_options::*member; int low, high; };
		constexpr setting integers[] = {{"music", &engine_options::music, 0, 10}, {"effects", &engine_options::effects, 0, 10},
			{"fullscreen", &engine_options::fullscreen, 0, 2},
			{"display_width", &engine_options::display_width, 0, 32768}, {"display_height", &engine_options::display_height, 0, 32768},
			{"refresh_millihz", &engine_options::refresh_millihz, 0, 1000000}, {"vsync", &engine_options::vsync, 0, 2}, {"fps_limit", &engine_options::fps, 0, 1000}, {"language", &engine_options::language, -1, 4}};
		constexpr option_entry root[] = {{option_id::sound}, {option_id::display}, {option_id::game}, {option_id::back}};
		constexpr option_entry sound[] = {{option_id::music}, {option_id::effects}, {option_id::back}};
		constexpr option_entry display[] = {{option_id::fullscreen}, {option_id::vsync}, {option_id::fps}, {option_id::loading}, {option_id::back}};
		constexpr option_entry exclusive[] = {{option_id::fullscreen}, {option_id::display_mode}, {option_id::vsync}, {option_id::fps}, {option_id::loading}, {option_id::back}};
		constexpr option_entry game[] = {{option_id::language}, {option_id::autosave}, {option_id::back}};
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
		out << "loading_screens=" << values.loading_screens << '\n';
		out.close();
		if(!out) return false;
		// copy_file replaces the old file only after the complete staging file was closed.
		std::filesystem::copy_file(temporary, root / "openrayman.cfg", std::filesystem::copy_options::overwrite_existing, ec);
		if(ec) return false;
		std::filesystem::remove(temporary, ec);
		return true;
	}
	std::span<const option_entry> options_page::entries(const engine_options& values) const
	{
		switch(current) { case page::sound: return sound; case page::display: if(!desktop_display) return std::span(display).subspan(1); return values.fullscreen == 2 ? std::span<const option_entry>(exclusive) : std::span<const option_entry>(display); case page::game: return game; default: return root; }
	}
	bool options_page::navigate(action input, engine_options& v, bool& autosave, bool has_slot)
	{
		const int count = int(entries(v).size());
		if(input == action::up) { selected = (selected + count - 1) % count; return false; }
		if(input == action::down) { selected = (selected + 1) % count; return false; }
		auto id = entries(v)[std::size_t(selected)].id;
		// Language is an action as well as a left/right value row.
		if(input == action::confirm && id == option_id::language) input = action::right;
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
		case option_id::fullscreen: v.fullscreen = (v.fullscreen + delta + 3) % 3; break;
		case option_id::display_mode:
			if(!modes.empty()) {
				auto it = std::find(modes.begin(), modes.end(), display_mode{v.display_width, v.display_height, v.refresh_millihz});
				int at = it == modes.end() ? 0 : int(it - modes.begin());
				const auto mode = modes[std::size_t((at + delta + int(modes.size())) % int(modes.size()))];
				v.display_width = mode.width; v.display_height = mode.height; v.refresh_millihz = mode.refresh_millihz;
			} break;
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
		const unsigned language = unsigned(std::max(v.language, 0));
		auto tr = [language](ui_key key) { return ui_text(key, language); };
		auto title = current == page::sound ? ui_key::sound_title : current == page::display ? ui_key::display_title : current == page::game ? ui_key::game_title : ui_key::options;
		slots.set(0, 500, 100, "/C:" + tr(title), 255, 20);
		int row = 0;
		for(auto entry : entries(v))
		{
			std::string value;
			switch(entry.id)
			{
			case option_id::music: value = std::to_string(v.music * 10) + "%"; break;
			case option_id::effects: value = std::to_string(v.effects * 10) + "%"; break;
			case option_id::fullscreen: value = tr(v.fullscreen == 0 ? ui_key::windowed : v.fullscreen == 1 ? ui_key::borderless : ui_key::exclusive); break;
			case option_id::display_mode: value = v.display_width ? std::to_string(v.display_width) + "x" + std::to_string(v.display_height) + " " + std::to_string(v.refresh_millihz / 1000) + "." + std::to_string(1000 + (v.refresh_millihz % 1000)).substr(1) + " Hz" : tr(ui_key::desktop); break;
			case option_id::vsync: value = tr(v.vsync == 0 ? ui_key::on : v.vsync == 1 ? ui_key::strict : ui_key::off); break;
			case option_id::fps: value = v.fps ? std::to_string(v.fps) : tr(ui_key::off); break;
			case option_id::loading: value = tr(v.loading_screens ? ui_key::on : ui_key::off); break;
			case option_id::language: value = ui_language_name(language); break;
			case option_id::autosave: value = tr(slot ? (autosave ? ui_key::on : ui_key::off) : ui_key::no_save); break;
			default: break;
			}
			const auto index = std::size_t(++row);
			auto label = tr(static_cast<ui_key>(entry.id)) + (value.empty() ? "" : "  " + value);
			float size = 20;
			for(; size > 7; size -= 0.5f) {
				auto metrics = screen_slot_metrics(size); int width = 0;
				for(char c : label) width += c == ' ' ? metrics.space : metrics.advance;
				if(width <= 720) break;
			}
			slots.set(index, 500, 350.0f + float(row - 1) * 100, "/C:" + label, 255, size);
			slots.set_field(index, 7, row - 1 == selected ? 3.0f : 1.0f);
			slots.set_field(index, 1, row - 1 == selected ? 1.0f : 0.0f);
		}
		if(!error.empty()) slots.set(11, 500, 920, "/C:/O200:" + error, 255, 8);
	}
}

