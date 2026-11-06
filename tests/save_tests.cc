#include "test_util.h"
#include <openrayman/save_file.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/file.h>
#include <openrayman/game_data.h>
#include <openrayman/game/saves.h>

#include <doctest/doctest.h>
#include <algorithm>

using namespace openrayman;

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

TEST_CASE("real data: installed save checksums, registry lengths and restoration")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto game = game_data::locate(*dir); REQUIRE(game);
	auto saves = game->directory("saves"); REQUIRE(saves);
	std::size_t checked = 0;
	for(const auto& folder : std::filesystem::directory_iterator(*saves))
	{
		if(!folder.is_directory()) continue;
		for(const auto& file : std::filesystem::directory_iterator(folder.path()))
		{
			if(file.path().extension() != ".sav") continue;
			CAPTURE(file.path().filename().string());
			save_file save(file.path());
			CHECK(save.checksum == 0);
			++checked;
			auto stem = file.path().stem().string();
			bool general = stem == "General";
			auto name = general ? "Learn_10" : stem.substr(0, stem.rfind('_'));
			auto files = load_level_files(*game, name);
			auto h = read_level_header(files);
			scene tree(files.memory, h);
			auto registry = read_save_registry(files.memory, h, tree);
			CHECK(save.bytes.size() == (general ? registry.general_size : registry.level_size));
			CHECK(registry.general_size == 196);
			if(general)
			{
				CHECK(save.bytes.size() == 196);
				REQUIRE(registry.general.size() == 55);
				// Structural metadata, never copies of Ubisoft save bytes.
				for(unsigned i = 0; i < 3; ++i)
				{
					CHECK(registry.general[i].standard_offset == 0x23u - i);
					CHECK(registry.general[i].offset == i);
					CHECK(registry.general[i].size == 1);
				}
				const std::pair<unsigned, unsigned> scalar_offsets[] = {
					{67, 3}, {50, 7}, {49, 8}, {46, 9}, {45, 10}, {20, 191}, {6, 192}
				};
				for(auto [variable, offset] : scalar_offsets)
				{
					auto it = std::ranges::find(registry.general, variable, &save_entry::variable);
					REQUIRE(it != registry.general.end());
					CHECK(it->offset == offset);
				}
				CHECK(registry.general[8].offset == 11);
				CHECK(registry.general[8].element == 44);
				CHECK(registry.general[52].offset == 187);
				CHECK(registry.general[52].element == 0);
				CHECK(std::ranges::count_if(registry.general, [](const auto& e) { return e.variable == 42; }) == 45);
			}
			game::live_memory memory(files.memory);
			game::interpreter vm(memory, h.placements.empty() ? 0 : h.placements.front().super_object.address());
			const auto& entries = general ? registry.general : registry.level;
			game::apply_save_entries(vm, tree, entries, save.bytes, general ? registry.general_size : registry.level_size);
			for(const auto& e : entries)
			{
				if(e.bit)
				{
					bool absent = (std::to_integer<unsigned>(save.bytes[e.offset]) >> *e.bit) & 1;
					CHECK(bool(memory.u8(e.address) & 1) == absent);
					CHECK(bool(memory.u8(e.address) & 2) == !absent);
					if(e.node) CHECK(vm.removed(tree.node(*e.node).at.address()) == absent);
				}
				else CHECK(std::ranges::equal(memory.span(e.address, e.size), std::span(save.bytes).subspan(e.offset, e.size)));
			}
		}
	}
	CHECK(checked > 0);
	if(auto options = game->directory("options"))
		if(auto cfg = find_case_insensitive(*options, "Current.cfg"))
		{
			save_file config(*cfg);
			CHECK(config.checksum == 0);
			CHECK(config.bytes.size() == 87);
		}
}

TEST_CASE("real data: chosen slot bypasses snapshots and type-5 restoration preserves other flags")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto game = game_data::locate(*dir); REQUIRE(game);
	auto files = load_level_files(*game, "Learn_10");
	auto h = read_level_header(files);
	scene tree(files.memory, h);
	auto registry = read_save_registry(files.memory, h, tree);
	REQUIRE(registry.level.size() == 2);
	CHECK(registry.level[0].node == 71);
	CHECK(registry.level[0].offset == 0);
	CHECK(registry.level[0].bit == 0);
	CHECK_FALSE(registry.level[1].node.has_value());
	CHECK(registry.level[1].bit == 1);
	game::live_memory memory(files.memory);
	game::interpreter vm(memory, h.placements.front().super_object.address());
	game::load_save_slot(vm, *game, files, h, tree, 1);
	CHECK(vm.removed(tree.node(71).at.address()));
	CHECK_FALSE(vm.removed(tree.node(3).at.address())); // the interim snapshot would remove this
	const auto& e = registry.level[0];
	memory.write<std::uint8_t>(e.address, 0xa5);
	std::vector<std::byte> zero(registry.level_size);
	game::apply_save_entries(vm, tree, registry.level, zero, registry.level_size);
	CHECK(memory.u8(e.address) == 0xa6);
	CHECK_FALSE(vm.removed(tree.node(71).at.address()));
	CHECK_THROWS_AS(game::apply_save_entries(vm, tree, registry.level, {}, registry.level_size), format_error);
	CHECK(memory.u8(e.address) == 0xa6);
	CHECK_THROWS_AS(save_slot_folder(*game->directory("saves"), 0), format_error);
	CHECK_THROWS_AS(save_slot_folder(*game->directory("saves"), 4), format_error);
}
