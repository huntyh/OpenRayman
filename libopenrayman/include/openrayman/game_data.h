#pragma once

#include <openrayman/dsb_script.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace openrayman
{
	// A Rayman 2 PC installation, located from its install folder or its Data folder.
	class game_data
	{
	public:
		// Returns nullopt if no Game.dsb is found in the folder or its Data subfolder.
		// Throws if Game.dsb exists but can't be parsed.
		static std::optional<game_data> locate(const std::filesystem::path& folder);

		// The folder containing Game.dsb.
		const std::filesystem::path& data_dir() const { return m_data_dir; }

		// Game.dsb contents: level list, data directories, texture archives...
		const dsb_script& game_dsb() const { return m_game_dsb; }

		// Resolves a game relative path ("\World\Sound") case-insensitively.
		std::optional<std::filesystem::path> resolve(const std::string& relative) const;

		// Resolves a directory by its Game.dsb role, e.g. "sound" or "levels".
		std::optional<std::filesystem::path> directory(const std::string& role) const;

		// The texture archives listed in Game.dsb that exist on disk.
		std::vector<std::filesystem::path> texture_archives() const;

		// All APM music files in the sound directory, sorted by name.
		std::vector<std::filesystem::path> music_files() const;

	private:
		std::filesystem::path m_data_dir;
		dsb_script m_game_dsb;
	};
}
