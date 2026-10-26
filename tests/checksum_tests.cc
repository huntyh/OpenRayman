// Regression checks against known-good decodes of real game files.
//
// Only checksums are stored here, never game data. Each value was recorded from a decode that was
// verified by other means (looked at, listened to, or measured), so a change in any of them means
// a decoder now produces different output. If that change is intended, re-verify the output and
// update the value. Skipped unless OPENRAYMAN_GAME_DIR is set.

#include "test_util.h"

#include <openrayman/apm_audio.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/file.h>
#include <openrayman/game_data.h>
#include <openrayman/gf_texture.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <span>

using namespace openrayman;
namespace fs = std::filesystem;

// FNV-1a, 64 bit. Not cryptographic, just a stable fingerprint of decoded output.
static std::uint64_t fnv1a(std::span<const std::uint8_t> data, std::uint64_t hash = 0xCBF29CE484222325ull)
{
	for(std::uint8_t b : data)
	{
		hash ^= b;
		hash *= 0x100000001B3ull;
	}
	return hash;
}

static std::uint64_t checksum(const gf_texture& t)
{
	std::uint32_t header[3] = { t.width, t.height, t.channels };
	std::uint64_t hash = fnv1a({ reinterpret_cast<const std::uint8_t*>(header), sizeof(header) });
	return fnv1a(t.rgba, hash);
}

static std::uint64_t checksum(const std::vector<std::int16_t>& pcm)
{
	return fnv1a({ reinterpret_cast<const std::uint8_t*>(pcm.data()), pcm.size() * sizeof(std::int16_t) });
}

struct texture_case
{
	const char* archive;
	const char* path;
	std::uint32_t width, height;
	std::uint8_t channels;
	std::uint64_t checksum;
};

struct music_case
{
	const char* file;
	std::uint16_t channels;
	std::size_t frames;
	std::uint64_t checksum;
};

// All checked by eye after decoding: loading screens, a character texture, and a water lily whose
// alpha channel cuts out cleanly over a checkerboard.
static const texture_case textures[] = {
	{ "Vignette.cnt", "Rayman.gf", 640, 480, 3, 17800487092383384409ull },
	{ "Vignette.cnt", "VigLoadLearn01.gf", 640, 480, 3, 3656100658832369951ull },
	{ "Textures.cnt", "textures_personnages/divers/ray_rt_reste_tete2.gf", 128, 128, 3, 10300903441806984627ull },
	{ "Textures.cnt", "textures_vegetaux/nenuphar_nz.gf", 128, 128, 4, 2427826098800105867ull },
};

// Measured free of one-sided clipping and DC drift. BA102 was one of the tracks that clipped on one
// side before the channel pairing fix; outro1 is the only mono file.
static const music_case music[] = {
	{ "main title.apm", 2, 4163566, 5727171558357249628ull },
	{ "BA102.apm", 2, 63774, 14905875563108085017ull },
	{ "outro1.apm", 1, 33102, 10645834919270604473ull },
};

TEST_CASE("checksums: known-good texture decodes")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping");
		return;
	}
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());

	for(const texture_case& c : textures)
	{
		INFO(c.archive, ": ", c.path);
		auto path = game->resolve(c.archive);
		REQUIRE(path.has_value());
		cnt_archive archive(*path);
		const cnt_entry* entry = archive.find(c.path);
		REQUIRE(entry != nullptr);

		gf_texture t = decode_gf(archive.read(*entry));
		CHECK(t.width == c.width);
		CHECK(t.height == c.height);
		CHECK(t.channels == c.channels);
		CHECK(checksum(t) == c.checksum);
	}
}

TEST_CASE("checksums: known-good music decodes")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping");
		return;
	}
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());
	auto sound = game->directory("sound");
	REQUIRE(sound.has_value());

	for(const music_case& c : music)
	{
		INFO(c.file);
		auto path = find_case_insensitive(*sound, c.file);
		REQUIRE(path.has_value());

		apm_decoder decoder(read_file(*path));
		CHECK(decoder.channels() == c.channels);
		CHECK(decoder.frames() == c.frames);
		CHECK(checksum(decoder.decode_all()) == c.checksum);
	}
}
