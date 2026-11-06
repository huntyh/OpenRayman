#include <openrayman/game/saves.h>
#include <openrayman/error.h>
#include <openrayman/file.h>

#include <algorithm>

namespace openrayman::game
{
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

	void load_save_slot(interpreter& vm, const game_data& game, const level_files& files,
		const level_header& header, const scene& tree, unsigned slot)
	{
		auto saves = game.directory("saves");
		if(!saves) throw format_error("installation has no save directory");
		auto folder = save_slot_folder(*saves, slot);
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
