#pragma once

#include <openrayman/game/interpreter.h>
#include <openrayman/game_data.h>
#include <openrayman/save_file.h>

namespace openrayman::game
{
	std::filesystem::path save_root();
	std::vector<std::byte> capture_save_entries(const live_memory& memory, std::span<const save_entry> entries, std::size_t size);
	// Restore registered bytes before the first actor tick. A missing level file leaves that
	// level at its initial state; a missing general file or malformed save is an error.
	void apply_save_entries(interpreter& vm, const scene& tree, std::span<const save_entry> entries,
		std::span<const std::byte> bytes, std::size_t expected_size);
	void load_save_slot(interpreter& vm, const std::filesystem::path& saves, const level_files& files,
		const level_header& header, const scene& tree, unsigned slot);
}
