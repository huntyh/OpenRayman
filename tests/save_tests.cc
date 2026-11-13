#include "test_util.h"
#include <openrayman/save_file.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/file.h>
#include <openrayman/game_data.h>
#include <openrayman/game/saves.h>
#include <openrayman/game/first_visit.h>
#include <random>

#include <doctest/doctest.h>
#include <algorithm>
#include <fstream>

using namespace openrayman;

TEST_CASE("language setting survives restart and rejects invalid files")
{
	const auto folder = std::filesystem::temp_directory_path() /
		("openrayman_language_test_" + std::to_string(std::random_device{}()));
	REQUIRE_FALSE(std::filesystem::exists(folder));
	CHECK_FALSE(game::read_language(folder));
	for(unsigned language = 0; language < 5; ++language)
	{
		REQUIRE(game::write_language(folder, language));
		CHECK(game::read_language(folder) == language);
	}
	CHECK_FALSE(game::write_language(folder, 5));
	CHECK(game::read_language(folder) == 4);
	{ std::ofstream file(folder / "language.txt"); file << "2 garbage"; }
	CHECK_FALSE(game::read_language(folder));
	std::filesystem::remove_all(folder);
}

// Synthetic wrapping only, no game data or implementation of the dictionary encoder.
static std::vector<std::byte> wrap(std::uint32_t length, const std::vector<unsigned>& tokens)
{
	unsigned sum = 0;
	for(auto v : tokens) sum += v;
	bytes out;
	out.u8(std::uint8_t(sum) ^ 0x53).u32(length ^ 0x54555657u);
	unsigned counter = 0x57;
	for(auto v : tokens)
	{
		unsigned reversed = 0;
		for(unsigned b = 0; b < 8; ++b) reversed |= (((counter & 255) >> b) & 1) << (7 - b);
		out.u8(std::uint8_t(v ^ reversed ^ 0xb9));
		++counter;
	}
	return out.data;
}

TEST_CASE("save wrapping: dictionary reuse, patches, final block and validation")
{
	auto full = save_file::decode(wrap(8, { 17, 0 }));
	CHECK(full.bytes.size() == 8);
	CHECK(std::ranges::all_of(full.bytes, [](auto b) { return b == std::byte(17); }));
	auto partial = save_file::decode(wrap(11, { 17, 1, 99, 17, 2, 88, 3 }));
	REQUIRE(partial.bytes.size() == 11);
	CHECK(partial.bytes[0] == std::byte(99));
	CHECK(partial.bytes[8] == std::byte(99));
	CHECK(partial.bytes[9] == std::byte(88));
	CHECK(partial.bytes[10] == std::byte(17));
	CHECK(partial.checksum == 0);
	CHECK(save_file::decode(wrap(0, {})).bytes.empty());
	std::vector<unsigned> many;
	for(unsigned i = 0; i < 150; ++i) { many.push_back(17); many.push_back(0); }
	auto long_save = save_file::decode(wrap(1200, many));
	CHECK(long_save.bytes.size() == 1200);
	CHECK(std::ranges::all_of(long_save.bytes, [](auto b) { return b == std::byte(17); }));
	auto corrupt = wrap(8, {17, 0}); corrupt[0] ^= std::byte(1);
	CHECK_THROWS_AS(save_file::decode(corrupt), format_error);
	CHECK_THROWS_AS(save_file::decode(wrap(8, {17, 255, 1})), format_error);
	CHECK_THROWS_AS(save_file::decode(wrap(7, {17, 0})), format_error);
	CHECK_THROWS_AS(save_file::decode(wrap(8, {17, 0, 9})), format_error);
	CHECK_THROWS_AS(save_file::decode(wrap(8, {17})), format_error);
	CHECK_THROWS_AS(save_file::decode(wrap(0xffffffffu, {})), format_error);
	CHECK_THROWS_AS(save_file::decode(std::span<const std::byte>{}), format_error);
}

TEST_CASE("save encoder round trips")
{
	std::mt19937 random(12345);
	for(std::size_t size : {0u, 1u, 7u, 8u, 9u, 196u, 1200u})
	{
		std::vector<std::byte> plain(size);
		for(auto& b : plain) b = std::byte(random() & 255);
		auto decoded = save_file::decode(save_file::encode(plain));
		CHECK(decoded.bytes == plain);
		CHECK(decoded.checksum == 0);
	}
}

TEST_CASE("real data: registry layout and generated mock restoration")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto game = game_data::locate(*dir); REQUIRE(game);
	auto files = load_level_files(*game, "Learn_10");
	auto h = read_level_header(files);
	scene tree(files.memory, h);
	auto registry = read_save_registry(files.memory, h, tree);
	CHECK(registry.general_size == 196);
	REQUIRE(registry.general.size() == 55);
	CHECK(registry.general[8].offset == 11);
	CHECK(registry.general[8].element == 44);
	CHECK(registry.general[52].offset == 187);
	CHECK(registry.general[52].element == 0);
	REQUIRE(registry.level.size() == 2);
	CHECK(registry.level[0].node == 71);
	game::live_memory source(files.memory);
	game::interpreter original(source, h.placements.front().super_object.address());
	std::vector<std::uint32_t> actors;
	for(const auto& a : h.placements) actors.push_back(a.super_object.address());
	for(auto bit : game::revisit_story_bits) REQUIRE(original.set_story_bit(actors, bit));
	game::apply_revisit_state(original, tree, "Learn_10");
	auto general = game::capture_save_entries(source, registry.general, registry.general_size);
	auto level = game::capture_save_entries(source, registry.level, registry.level_size);
	auto folder = std::filesystem::current_path() / "mock_save_test";
	std::filesystem::create_directories(folder);
	save_file::write(folder / "General.sav", general);
	save_file::write(folder / "Learn_10_0.sav", level);
	save_file decoded_general(folder / "General.sav");
	save_file decoded_level(folder / "Learn_10_0.sav");
	std::filesystem::remove(folder / "General.sav");
	std::filesystem::remove(folder / "Learn_10_0.sav");
	std::filesystem::remove(folder);
	game::live_memory target(files.memory);
	game::interpreter restored(target, h.placements.front().super_object.address());
	game::apply_save_entries(restored, tree, registry.level, decoded_level.bytes, registry.level_size);
	game::apply_save_entries(restored, tree, registry.general, decoded_general.bytes, registry.general_size);
	CHECK(restored.removed(tree.node(71).at.address()));
	CHECK(game::capture_save_entries(target, registry.general, registry.general_size) == general);
	CHECK(game::capture_save_entries(target, registry.level, registry.level_size) == level);
}
