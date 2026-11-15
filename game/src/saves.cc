#include <openrayman/game/saves.h>
#include <openrayman/input.h>
#include <openrayman/game/dialog.h>
#include <cctype>
#include <random>
#include <openrayman/error.h>
#include <openrayman/file.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <ctime>
#include <chrono>
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace openrayman::game
{
	std::optional<unsigned> read_language(const std::filesystem::path& folder)
	{
		std::ifstream file(folder / "language.txt");
		unsigned language;
		if(!(file >> language) || language > 4) return std::nullopt;
		file >> std::ws;
		if(!file.eof()) return std::nullopt;
		return language;
	}

	bool write_language(const std::filesystem::path& folder, unsigned language)
	{
		if(language > 4) return false;
		std::error_code error;
		std::filesystem::create_directories(folder, error);
		if(error) return false;
		std::ofstream file(folder / "language.txt", std::ios::trunc);
		file << language << '\n';
		file.close();
		return bool(file);
	}

	std::filesystem::path save_root()
	{
		if(const char* p = std::getenv("OPENRAYMAN_SAVE_DIR"); p && *p) return p;
#ifdef _WIN32
		std::wstring executable(32768, L'\0');
		auto n = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
		if(!n || n == executable.size()) throw format_error("cannot locate executable");
		executable.resize(n);
		auto directory = std::filesystem::path(executable).parent_path();
#else
		auto directory = std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
		if(std::filesystem::exists(directory / "portable.txt")) return directory / "SaveGame";
#ifdef _WIN32
		if(const char* p = std::getenv("APPDATA"); p && *p) return std::filesystem::path(p) / "OpenRayman" / "OpenRayman" / "SaveGame";
#else
		if(const char* p = std::getenv("XDG_DATA_HOME"); p && *p) return std::filesystem::path(p) / "OpenRayman" / "OpenRayman" / "SaveGame";
		if(const char* p = std::getenv("HOME"); p && *p) return std::filesystem::path(p) / ".local/share/OpenRayman/OpenRayman/SaveGame";
#endif
		throw format_error("cannot find per-user save directory");
	}

	std::vector<std::byte> capture_save_entries(const live_memory& memory, std::span<const save_entry> entries, std::size_t size)
	{
		std::vector<std::byte> result(size);
		for(const auto& e : entries)
		{
			if(e.offset > size || (e.bit ? e.offset == size : e.size > size - e.offset) || !memory.contains(e.address, e.bit ? 1 : e.size)) throw format_error("invalid save registry entry");
			if(e.bit) { if(memory.u8(e.address) & 1) result[e.offset] |= std::byte(1u << *e.bit); }
			else std::ranges::copy(memory.span(e.address, e.size), result.begin() + e.offset);
		}
		return result;
	}

	void apply_save_entries(interpreter& vm, const scene& tree, std::span<const save_entry> entries,
		std::span<const std::byte> bytes, std::size_t expected_size)
	{
		if(bytes.size() != expected_size) throw format_error("save does not match the loaded registry size");
		auto& memory = vm.memory();
		// Validate every destination before changing any state.
		for(const auto& e : entries)
			if(e.offset > bytes.size() || (e.bit ? e.offset == bytes.size() : e.size > bytes.size() - e.offset)
				|| !memory.contains(e.address, e.bit ? 1 : e.size))
				throw format_error("save registry entry is outside its file or memory");
		for(const auto& e : entries)
		{
			if(!e.bit)
			{
				std::ranges::copy(bytes.subspan(e.offset, e.size), memory.span(e.address, e.size).begin());
				continue;
			}
			bool absent = (std::to_integer<unsigned>(bytes[e.offset]) & (1u << *e.bit)) != 0;
			// 0x44A500 merges the stored bit; 0x4132A0 mode 3 then adjusts type-5 activity.
			auto flags = memory.u8(e.address);
			flags = std::uint8_t(absent ? (flags | 1u) & ~2u : (flags & ~1u) | 2u);
			memory.write<std::uint8_t>(e.address, flags);
			auto so = e.node ? tree.node(*e.node).at.address() : vm.super_object_of(e.actor.address());
			if(so)
			{
				if(absent) vm.remove(so);
				else vm.restore(so);
			}
		}
	}

	void load_save_slot(interpreter& vm, const std::filesystem::path& saves, const level_files& files,
		const level_header& header, const scene& tree, unsigned slot)
	{
		auto folder = save_slot_folder(saves, slot);
		auto general_path = find_case_insensitive(folder, "General.sav");
		if(!general_path) throw format_error("selected slot has no General.sav");
		auto registry = read_save_registry(files.memory, header, tree);
		save_file general(*general_path);
		auto level_path = find_case_insensitive(folder, files.name + "_0.sav");
		std::optional<save_file> level;
		if(level_path) level.emplace(*level_path);
		if(general.bytes.size() != registry.general_size || (level && level->bytes.size() != registry.level_size))
			throw format_error("selected save does not match the loaded level registry");
		if(level) apply_save_entries(vm, tree, registry.level, level->bytes, registry.level_size);
		apply_save_entries(vm, tree, registry.general, general.bytes, registry.general_size);
	}
}
namespace openrayman::game
{
	namespace
	{
		bool inside(const std::filesystem::path& target, const std::filesystem::path& base)
		{
			auto t = std::filesystem::weakly_canonical(target).generic_wstring();
			auto b = std::filesystem::weakly_canonical(base).generic_wstring();
#ifdef _WIN32
			std::ranges::transform(t, t.begin(), [](wchar_t c) { return std::towlower(c); });
			std::ranges::transform(b, b.begin(), [](wchar_t c) { return std::towlower(c); });
#endif
			return t == b || (t.size() > b.size() && t.compare(0, b.size(), b) == 0 && t[b.size()] == L'/');
		}

		std::string short_name(std::string text)
		{
			while(text.starts_with('/'))
			{
				auto end = text.find(':');
				if(end == std::string::npos) return {};
				text.erase(0, end + 1);
			}
			text.resize(std::min<std::size_t>(3, text.size()));
			return text;
		}

		std::pair<std::string, unsigned> slot_info(const std::filesystem::path& root, unsigned slot)
		{
			std::ifstream in(root / ("Slot" + std::to_string(slot)) / "slot.txt");
			std::string name = "RAY";
			unsigned completion = 0;
			std::getline(in, name);
			if(name.empty() || name.size() > 3) name = "RAY";
			in >> completion;
			return {name, std::min(1000u, completion)};
		}
	}

	// Own implementation of the slot publication performed by 0x410870/0x4110E0.
	// Build a complete replacement beside the slot; a failed encode/write never touches it.
	void write_save_slot(const std::filesystem::path& root, const std::filesystem::path& game_folder,
		unsigned slot, std::span<const std::byte> general,
		const std::map<std::string, std::vector<std::byte>>& levels, const std::string& name, unsigned completion, bool fresh)
	{
		namespace fs = std::filesystem;
		auto protected_folder = game_folder;
		if(protected_folder.filename() == "Data" || protected_folder.filename() == "data") protected_folder = protected_folder.parent_path();
		if(slot < 1 || slot > 3) throw format_error("save slot must be 1, 2 or 3");
		auto dest = root / ("Slot" + std::to_string(slot));
		if(inside(root, protected_folder) || inside(dest, protected_folder)) throw format_error("save destination must be outside the game folder");
		for(const auto& [level, bytes] : levels)
			if(level.empty() || !std::ranges::all_of(level, [](unsigned char c) { return std::isalnum(c) || c == '_'; }))
				throw format_error("invalid save level name");
		fs::create_directories(root);
		auto stage = root / (".Slot" + std::to_string(slot) + "-" + std::to_string(std::random_device{}()));
		auto backup = stage; backup += ".previous";
		if(!fs::create_directory(stage)) throw format_error("cannot stage save slot");
		bool moved = false;
		try
		{
			if(!fresh && fs::exists(dest))
				for(const auto& entry : fs::directory_iterator(dest))
					if(entry.is_regular_file() && entry.path().extension() == ".sav")
					{
						if(inside(entry.path(), protected_folder)) throw format_error("save file points inside the game folder");
						fs::copy_file(entry.path(), stage / entry.path().filename());
					}
			for(const auto& [level, bytes] : levels) save_file::write(stage / (level + "_0.sav"), bytes);
			save_file::write(stage / "General.sav", general);
			std::ofstream info(stage / "slot.txt", std::ios::binary);
			info << short_name(name) << '\n' << std::min(1000u, completion) << '\n';
			info.close();
			if(!info) throw format_error("cannot write slot metadata");
			if(fs::exists(dest)) { fs::rename(dest, backup); moved = true; }
			try { fs::rename(stage, dest); }
			catch(...) { if(moved) fs::rename(backup, dest); throw; }
		}
		catch(...)
		{
			std::error_code ignored;
			// stage is a newly created child of our save root, never a supplied path.
			fs::remove_all(stage, ignored);
			throw;
		}
		std::error_code ignored;
		if(moved) fs::remove_all(backup, ignored);
	}

	namespace
	{
		std::filesystem::path autosave_file(const std::filesystem::path& folder, unsigned slot)
		{
			return folder / ("autosave_Slot" + std::to_string(slot) + ".txt");
		}
	}

	std::optional<bool> read_autosave(const std::filesystem::path& folder, unsigned slot)
	{
		std::ifstream in(autosave_file(folder, slot));
		unsigned choice;
		if(!(in >> choice) || choice > 1) return std::nullopt;
		in >> std::ws;
		if(!in.eof()) return std::nullopt;
		return choice != 0;
	}

	bool write_autosave(const std::filesystem::path& folder, unsigned slot, bool enabled)
	{
		std::error_code error;
		std::filesystem::create_directories(folder, error);
		if(error) return false;
		std::ofstream out(autosave_file(folder, slot), std::ios::trunc);
		out << (enabled ? 1 : 0) << '\n';
		out.close();
		return bool(out);
	}

	void forget_autosave(const std::filesystem::path& folder, unsigned slot)
	{
		std::error_code ignored;
		std::filesystem::remove(autosave_file(folder, slot), ignored);
	}

	void update_save_ui(save_session& session, const std::filesystem::path& folder, bool left, bool right, bool enter)
	{
		if(session.saving_ticks) --session.saving_ticks;
		if(session.explaining)
		{
			// After Yes: the Lum stays shown with its explanation; Enter lets it fade and go on.
			session.saving_ticks = std::max(session.saving_ticks, 31u);
			if(enter && !session.enter_down)
			{
				session.explaining = false;
				session.saving_ticks = 30;
			}
		}
		else if(session.question)
		{
			if((left && !session.left_down) || (right && !session.right_down)) session.choice = !session.choice;
			if(enter && !session.enter_down)
			{
				if(write_autosave(folder, session.slot, session.choice))
				{
					session.autosave = session.choice;
					session.question = false;
					if(session.choice)
					{
						session.explaining = true;
						session.saving_ticks = 31;
					}
				}
				else session.error = "cannot store autosave preference";
			}
		}
		session.left_down = left; session.right_down = right; session.enter_down = enter;
	}

	float saving_alpha(const save_session& session)
	{
		return std::min(1.0f, session.saving_ticks / 30.0f);
	}

	void install_save_handlers(interpreter& vm, const level_files& files, const level_header& header,
		const scene& tree, const std::filesystem::path& game_folder, std::shared_ptr<save_session> session)
	{
		auto registry = read_save_registry(files.memory, header, tree);
		auto level = files.name;
		auto root = std::filesystem::path(vm.save_folder);
		// The autosave choice of the slot being played (reloaded when the slot changes).
		auto settings = [=] {
			if(session->slot && session->settings_slot != session->slot)
			{
				session->autosave = read_autosave(root, session->slot);
				session->settings_slot = session->slot;
			}
		};
		settings();
		session->automatic_actor = 0;
		auto confirm_action = read_input_actions(files).first.address() + 9 * input_action_size;
		// The Hall actor's original Yes -> write -> success -> resume path still runs.
		// Only its confirmation conditions and text are replaced while autosave is enabled.
		vm.on_screen_text = [=, &vm](std::uint32_t actor, const value& text) {
			if(text.type != value_type::id) return false;
			settings();
			auto id = text.word();
			bool hall_question = id == 0x18F || id == 0x190;
			bool exit_question = id == 0x5043 && (level == "Menu" || level == "menu") && vm.get_variable(actor, 55).as_int() == 6 && vm.get_variable(actor, 40).as_int() != 0;
			if(session->autosave.value_or(false) && session->slot && (hall_question || exit_question))
			{
				if(session->automatic_actor != actor) session->automatic_saved = false;
				session->automatic_actor = actor;
				if(hall_question) vm.set_variable(actor, 7, value::integer(1));
				if(exit_question) vm.set_variable(actor, 41, value::integer(1));
				return true;
			}
			if(id == 0x183)
			{
				if(session->automatic_actor == actor) { session->automatic_saved = true; return true; }
				if((session->question || session->explaining) && session->question_actor == actor) return true; // still replaced by our screens
				if(session->first_save)
				{
					session->first_save = false;
					if(!session->autosave)
					{
						// Ours: the autosave question takes the place of the original "saved
						// successfully, press Enter" text; one Enter answers it and goes on.
						session->question = true; session->question_actor = actor; session->choice = false;
						return true;
					}
				}
			}
			return false;
		};
		vm.on_input_condition = [=](std::uint32_t actor, unsigned condition, std::uint32_t action) -> std::optional<bool> {
			if((session->question || session->explaining) && actor == session->question_actor) return false;
			if(session->automatic_actor == actor && condition == 45 && action == confirm_action)
			{
				if(session->automatic_saved) session->automatic_actor = 0;
				return true;
			}
			return std::nullopt;
		};
		// 90/93/94: 0x47D830. Both success paths return a nonzero byte.
		for(unsigned index : {90u, 93u})
			vm.on_function(index, [=](call& c, value& out) {
				value arg;
				c.args = c.vm.eval(c.self, c.args, arg);
				unsigned slot = unsigned(arg.as_int());
				if(!slot) slot = session->slot ? session->slot : 1;
				out = value::integer(0);
				try
				{
					auto general = capture_save_entries(c.vm.memory(), registry.general, registry.general_size);
					if(index == 93)
					{
						// 0x4054D0 -> 0x47D9C0 -> 0x466950 resets designer values at
						// the following level change. Save those values for the new slot now.
						auto& m = c.vm.memory();
						for(const auto& e : registry.general)
						{
							if(e.standard_offset)
							{
								// Menu behaviour 16 sets Rayman's health/max to 15/30 on continue.
								if(*e.standard_offset == 0x21) general[e.offset] = std::byte(15);
								if(*e.standard_offset == 0x22) general[e.offset] = std::byte(30);
								continue;
							}
							auto mind = m.u32(m.u32(e.actor.address() + 0x0C));
							auto vars = m.u32(mind + 0x0C);
							auto initial = m.u32(vars + 4);
							if(!initial) initial = m.u32(m.u32(m.u32(mind) + 8));
							auto source = initial + e.address - m.u32(vars + 8);
							std::ranges::copy(m.span(source, e.size), general.begin() + e.offset);
						}
					}

					auto levels = index == 93 ? std::map<std::string, std::vector<std::byte>>{} : session->levels;
					levels[level] = capture_save_entries(c.vm.memory(), registry.level, registry.level_size);
					write_save_slot(root, game_folder, slot, general, levels, session->name, session->completion, index == 93);
					session->slot = slot;
					if(index == 93)
					{
						// A new game in this slot asks again on its first success screen.
						forget_autosave(root, slot);
						session->autosave = std::nullopt;
						session->settings_slot = slot;
					}
					else settings();
					session->levels = std::move(levels);
					session->new_game = index == 93;
					session->first_save = index == 93;
					if(session->automatic_actor == c.self) session->saving_ticks = 390; // about 6.5 s, the last half second fading
					session->error.clear();
					out = value::integer(1);
				}
				catch(const std::exception& e) { session->error = e.what(); session->automatic_actor = 0; }
				return c.args;
			});
		// 91: 0x47D830 case 0x5B. Load a slot: 0x410F20 copies it to the working slot, 0x410410
		// reads its General.sav into memory, and the level files apply at the next level change.
		// A slot of 0 does nothing and fails.
		vm.on_function(91, [=, tree = std::make_shared<scene>(tree)](call& c, value& out) {
			value arg;
			c.args = c.vm.eval(c.self, c.args, arg);
			unsigned slot = unsigned(arg.as_int());
			out = value::integer(0);
			if(slot < 1 || slot > 3) return c.args;
			try
			{
				auto folder = save_slot_folder(root, slot);
				auto general_path = find_case_insensitive(folder, "General.sav");
				if(!general_path) return c.args;
				save_file general(*general_path);
				if(general.bytes.size() != registry.general_size) return c.args;
				std::map<std::string, std::vector<std::byte>> levels;
				for(const auto& file : std::filesystem::directory_iterator(folder))
				{
					auto name = file.path().filename().string();
					if(name.size() <= 6 || !name.ends_with("_0.sav")) continue;
					save_file level_save(file.path());
					levels[name.substr(0, name.size() - 6)] = level_save.bytes; // each level checks its own size
				}
				apply_save_entries(c.vm, *tree, registry.general, general.bytes, registry.general_size);
				session->slot = slot;
				settings();
				session->levels = std::move(levels);
				session->loaded = true;
				session->new_game = false;
				session->name = slot_info(root, slot).first;
				session->error.clear();
				out = value::integer(1);
			}
			catch(const std::exception& e) { session->error = e.what(); }
			return c.args;
		});
		vm.on_function(94, [=](call& c, value& out) { out = value::integer(session->slot); return c.args; });
		// 95: 0x47D730 skips leading formatting, retains three bytes and returns a
		// colliding slot (excluding the third argument), or zero. Argument two is evaluated.
		vm.on_function(95, [=](call& c, value& out) {
			value text, unused, excluded;
			c.args = c.vm.eval(c.self, c.args, text);
			c.args = c.vm.eval(c.self, c.args, unused);
			c.args = c.vm.eval(c.self, c.args, excluded);
			session->name = short_name(resolve_dialog_value(c.vm, text).value_or("RAY"));
			out = value::integer(0);
			for(unsigned slot = 1; slot <= 3; ++slot)
				if(slot != unsigned(excluded.as_int()) && std::filesystem::exists(root / ("Slot" + std::to_string(slot)) / "General.sav")
					&& slot_info(root, slot).first == session->name) out = value::integer(slot);
			return c.args;
		});
		// 431: 0x4702A0. Slot label and completion count (0..1000).
		vm.on_procedure(431, [=](call& c) {
			value slot, text, completion;
			c.args = c.vm.eval(c.self, c.args, slot);
			c.args = c.vm.eval(c.self, c.args, text);
			c.args = c.vm.eval(c.self, c.args, completion);
			session->name = short_name(resolve_dialog_value(c.vm, text).value_or("RAY"));
			session->completion = unsigned(std::clamp(completion.as_int(), 0, 1000));
			return c.args;
		});
		// 215..217: 0x478E10. Text returned via a fixed-size per-query buffer.
		auto buffer = vm.memory().allocate(32);
		for(unsigned index : {215u, 216u, 217u})
			vm.on_function(index, [=](call& c, value& out) {
				value arg; c.args = c.vm.eval(c.self, c.args, arg);
				unsigned slot = unsigned(arg.as_int());
				if(!slot) slot = session->slot ? session->slot : 1;
				std::string text;
				if(slot <= 3)
				{
					auto [name, completion] = slot_info(root, slot);
					if(index == 216) text = name;
					else if(index == 217) text = std::to_string(completion / 10) + "." + std::to_string(completion % 10);
					else
					{
						// 0x410DB0: local modification date of General.sav, at most 11 bytes.
						std::error_code error;
						auto time = std::filesystem::last_write_time(root / ("Slot" + std::to_string(slot)) / "General.sav", error);
						if(!error)
						{
							auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
								time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
							auto seconds = std::chrono::system_clock::to_time_t(system_time);
							std::tm local{};
#ifdef _WIN32
							localtime_s(&local, &seconds);
#else
							localtime_r(&seconds, &local);
#endif
							char date[32]{};
							std::strftime(date, sizeof(date), "%d/%m %H:%M", &local);
							text = date;
						}
					}
				}
				auto target = c.vm.memory().span(buffer, 32);
				std::ranges::fill(target, std::byte{});
				std::copy(text.begin(), text.end(), reinterpret_cast<char*>(target.data()));
				out = value::of(value_type::text, buffer);
				return c.args;
			});
	}
}
