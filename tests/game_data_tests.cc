// Checks against a real Rayman 2 installation. Skipped unless OPENRAYMAN_GAME_DIR is set.

#include "test_util.h"

#include <openrayman/apm_audio.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/dsb_script.h>
#include <openrayman/file.h>
#include <openrayman/game_data.h>
#include <openrayman/gf_texture.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>

using namespace openrayman;
namespace fs = std::filesystem;

static std::optional<game_data> real_game()
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping checks against real game data");
		return std::nullopt;
	}
	auto game = game_data::locate(*dir);
	REQUIRE_MESSAGE(game.has_value(), "no Game.dsb in OPENRAYMAN_GAME_DIR");
	return game;
}

TEST_CASE("real data: Game.dsb lists every level, and each one exists")
{
	auto game = real_game();
	if(!game)
		return;

	const dsb_script& dsb = game->game_dsb();
	CHECK(dsb.levels.size() == 70);
	CHECK(dsb.levels.front() == "Menu");
	CHECK(dsb.levels.back() == "Raycap");
	CHECK(dsb.texture_archives.size() == 2);

	auto levels = game->directory("levels");
	REQUIRE(levels.has_value());
	for(const std::string& level : dsb.levels)
	{
		INFO(level);
		CHECK(find_case_insensitive(*levels, level + "/" + level + ".sna").has_value());

		auto level_dsb = find_case_insensitive(*levels, level + "/" + level + ".dsb");
		REQUIRE(level_dsb.has_value());
		CHECK_NOTHROW(parse_dsb(read_file(*level_dsb)));
	}
}

TEST_CASE("real data: every texture decodes")
{
	auto game = real_game();
	if(!game)
		return;

	std::size_t total = 0;
	for(const fs::path& path : game->texture_archives())
	{
		cnt_archive archive(path);
		INFO(path.filename().string());
		std::string name = path.filename().string();
		std::ranges::transform(name, name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		if(name == "vignette.cnt")
			CHECK(archive.entries().size() == 35);
		else
			CHECK(archive.entries().size() == 1636);
		for(const cnt_entry& entry : archive.entries())
		{
			INFO(entry.path);
			gf_texture texture = decode_gf(archive.read(entry));
			CHECK(texture.rgba.size() == std::size_t(texture.width) * texture.height * 4);
			total++;
		}
	}
	CHECK(total == 1671);
}

TEST_CASE("real data: every music file decodes")
{
	auto game = real_game();
	if(!game)
		return;

	auto files = game->music_files();
	CHECK(files.size() == 1284);
	for(const fs::path& file : files)
	{
		INFO(file.filename().string());
		apm_decoder decoder(read_file(file));
		CHECK(decoder.frames() > 0);
		auto pcm = decoder.decode_all();
		CHECK(pcm.size() == decoder.frames() * decoder.channels());

		// A channel decoded from the wrong starting state drifts into the rails and clips
		// constantly. Real tracks clip a handful of samples at most.
		auto clipped = std::ranges::count_if(pcm, [](std::int16_t s) { return s == 32767 || s == -32768; });
		CHECK(clipped * 1000 < static_cast<std::ptrdiff_t>(pcm.size()));

		// No stereo track plays in only one ear (a channel pairing bug once silenced one side).
		if(decoder.channels() == 2)
		{
			double energy[2] = {};
			for(std::size_t i = 0; i < pcm.size(); i++)
				energy[i % 2] += double(pcm[i]) * pcm[i];
			if(energy[0] + energy[1] > 0)
			{
				CHECK(energy[0] > 0.01 * energy[1]);
				CHECK(energy[1] > 0.01 * energy[0]);
			}
		}
	}
}
