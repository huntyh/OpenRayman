#include <openrayman/game/saves.h>
#include <openrayman/error.h>
#include <openrayman/file.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#ifdef _WIN32
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
