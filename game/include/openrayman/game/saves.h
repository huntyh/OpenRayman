#pragma once

#include <openrayman/game/interpreter.h>
#include <openrayman/game_data.h>
#include <openrayman/save_file.h>
#include <memory>

namespace openrayman::game
{
	// Persists across level interpreters; bytes are captured before leaving each level.
	struct save_session
	{
		unsigned slot = 0;
		std::string name = "RAY", error;
		unsigned completion = 0;
		bool new_game = false;
		std::optional<bool> autosave;
		bool settings_loaded = false, first_save = false, question = false, choice = false;
		bool left_down = false, right_down = false, enter_down = false;
		std::uint32_t automatic_actor = 0, question_actor = 0;
		bool automatic_saved = false;
		unsigned saving_ticks = 0;
		std::map<std::string, std::vector<std::byte>> levels;
	};
	std::optional<bool> read_autosave(const std::filesystem::path& folder);
	bool write_autosave(const std::filesystem::path& folder, bool enabled);
	void update_save_ui(save_session& session, const std::filesystem::path& folder, bool left, bool right, bool enter);
	float saving_alpha(const save_session& session);
	void install_save_handlers(interpreter& vm, const level_files& files, const level_header& header,
		const scene& tree, const std::filesystem::path& game_folder, std::shared_ptr<save_session> session);
	void write_save_slot(const std::filesystem::path& root, const std::filesystem::path& game_folder,
		unsigned slot, std::span<const std::byte> general,
		const std::map<std::string, std::vector<std::byte>>& levels, const std::string& name, unsigned completion,
		bool fresh = false);
	std::filesystem::path save_root();
	std::optional<unsigned> read_language(const std::filesystem::path& folder);
	bool write_language(const std::filesystem::path& folder, unsigned language);
	std::vector<std::byte> capture_save_entries(const live_memory& memory, std::span<const save_entry> entries, std::size_t size);
	// Restore registered bytes before the first actor tick. A missing level file leaves that
	// level at its initial state; a missing general file or malformed save is an error.
	void apply_save_entries(interpreter& vm, const scene& tree, std::span<const save_entry> entries,
		std::span<const std::byte> bytes, std::size_t expected_size);
	void load_save_slot(interpreter& vm, const std::filesystem::path& saves, const level_files& files,
		const level_header& header, const scene& tree, unsigned slot);
}
