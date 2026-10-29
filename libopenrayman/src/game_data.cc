// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/game_data.h>
#include <openrayman/file.h>

#include <algorithm>
#include <cctype>

namespace openrayman
{
	std::optional<game_data> game_data::locate(const std::filesystem::path& folder)
	{
		for(const char* candidate : { "Game.dsb", "Data/Game.dsb" })
		{
			if(auto dsb = find_case_insensitive(folder, candidate))
			{
				game_data data;
				data.m_data_dir = dsb->parent_path();
				data.m_game_dsb = parse_dsb(read_file(*dsb));
				return data;
			}
		}
		return std::nullopt;
	}

	std::optional<std::filesystem::path> game_data::resolve(const std::string& relative) const
	{
		return find_case_insensitive(m_data_dir, relative);
	}

	std::optional<std::filesystem::path> game_data::directory(const std::string& role) const
	{
		auto it = m_game_dsb.directories.find(role);
		if(it == m_game_dsb.directories.end())
			return std::nullopt;
		return resolve(it->second);
	}

	std::vector<std::filesystem::path> game_data::texture_archives() const
	{
		std::vector<std::filesystem::path> result;
		for(const std::string& archive : m_game_dsb.texture_archives)
		{
			if(auto path = resolve(archive))
				result.push_back(*path);
		}
		return result;
	}

	std::vector<std::filesystem::path> game_data::music_files() const
	{
		std::vector<std::filesystem::path> result;
		auto sound = directory("sound");
		if(!sound)
			return result;

		for(const auto& entry : std::filesystem::directory_iterator(*sound))
		{
			std::string ext = entry.path().extension().string();
			std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if(entry.is_regular_file() && ext == ".apm")
				result.push_back(entry.path());
		}
		std::ranges::sort(result);
		return result;
	}

	std::map<std::uint32_t, std::filesystem::path> game_data::sound_banks() const
	{
		// The loader builds the names with "bnk_%d.BNM" (FUN_00453870); on disk they are
		// Bnk_<id>.bnm. The id in the name is the bank number the sound ids refer to.
		std::map<std::uint32_t, std::filesystem::path> result;
		auto sound = directory("sound");
		if(!sound)
			return result;

		for(const auto& entry : std::filesystem::directory_iterator(*sound))
		{
			if(!entry.is_regular_file() || entry.path().extension() != ".bnm")
				continue;
			const std::string stem = entry.path().stem().string();
			auto underscore = stem.find('_');
			if(underscore == std::string::npos)
				continue;
			const std::string digits = stem.substr(underscore + 1);
			if(digits.empty() || !std::ranges::all_of(digits, [](unsigned char c) { return std::isdigit(c) != 0; }))
				continue;
			result.emplace(static_cast<std::uint32_t>(std::stoul(digits)), entry.path());
		}
		return result;
	}

	std::optional<std::filesystem::path> game_data::sound_bank(std::uint32_t id) const
	{
		auto banks = sound_banks();
		auto it = banks.find(id);
		if(it == banks.end())
			return std::nullopt;
		return it->second;
	}
}
