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
#include <openrayman/game/handlers.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/game/dialog.h>
#include <openrayman/game/input.h>
#include <openrayman/brain.h>

TEST_CASE("save slot publication preserves other levels, clears a new game and rejects unsafe destinations")
{
	auto root = game::save_root() / ("write-test-" + std::to_string(std::random_device{}()));
	auto forbidden = root / "game";
	std::vector<std::byte> general{std::byte(12), std::byte(30)};
	game::write_save_slot(root, forbidden, 1, general, {{"Learn_10", {std::byte(3)}}}, "RAY", 250);
	CHECK(save_file(root / "Slot1/General.sav").bytes == general);
	CHECK_THROWS_AS(save_file::write(root / "Slot1", general), format_error);
	game::write_save_slot(root, forbidden, 1, general, {{"Learn_30", {std::byte(1)}}}, "ABC", 300);
	CHECK(save_file(root / "Slot1/Learn_10_0.sav").bytes == std::vector<std::byte>{std::byte(3)});
	CHECK(save_file(root / "Slot1/Learn_30_0.sav").bytes == std::vector<std::byte>{std::byte(1)});
	CHECK_THROWS(game::write_save_slot(root, forbidden, 4, {}, {}, "BAD", 0));
	CHECK_THROWS(game::write_save_slot(root, forbidden, 1, {}, {{"../escape", {}}}, "BAD", 0));
	CHECK(save_file(root / "Slot1/General.sav").bytes == general);
	CHECK_THROWS(game::write_save_slot(forbidden / "saves", forbidden, 1, {}, {}, "BAD", 0));
	CHECK_FALSE(std::filesystem::exists(forbidden));
	game::write_save_slot(root, forbidden, 1, {}, {{"Menu", {}}}, "NEW", 0, true);
	CHECK_FALSE(std::filesystem::exists(root / "Slot1/Learn_10_0.sav"));
	CHECK(save_file(root / "Slot1/General.sav").bytes.empty());
	std::filesystem::remove_all(root);
}

TEST_CASE("real data: scripted new game saves successfully and registered fields round trip")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto data = game_data::locate(*dir); REQUIRE(data);
	auto files = load_level_files(*data, "Menu");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto registry = read_save_registry(files.memory, header, tree);
	game::live_memory memory(files.memory);
	game::interpreter vm(memory, header.main_actor->address());
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	auto root = game::save_root() / ("script-test-" + std::to_string(std::random_device{}()));
	vm.save_folder = root.string();
	auto session = std::make_shared<game::save_session>();
	game::install_save_handlers(vm, files, header, tree, *dir, session);
	std::vector<std::uint32_t> actors;
	for(const auto& n : tree.nodes()) if(n.type == super_object_type::actor) actors.push_back(n.at.address());
	vm.set_actors(actors);
	game::input keys(memory, read_input_actions(files));
	for(unsigned tick = 0; tick < 1000; ++tick)
	{
		keys.set_key(0x1C, tick == 240 || tick == 600 || tick == 900);
		keys.step();
		game::step(vm, actors, [](std::uint32_t, const std::exception& e) { FAIL(e.what()); });
	}
	INFO(session->error);
	REQUIRE(session->slot == 1);
	CHECK(vm.get_variable(tree.node(6).at.address(), 33).as_int() == 1);
	// Autosave must not answer the separate Quit confirmation or turn it into another save.
	session->autosave = true;
	auto menu = tree.node(6).at.address();
	vm.set_variable(menu, 55, game::value::integer(6));
	vm.set_variable(menu, 40, game::value::integer(0));
	CHECK_FALSE(vm.on_screen_text(menu, game::value::of(game::value_type::id, 0x5043)));
	vm.set_variable(menu, 40, game::value::integer(1));
	CHECK(vm.on_screen_text(menu, game::value::of(game::value_type::id, 0x5043)));
	session->automatic_actor = 0;
	REQUIRE(std::filesystem::exists(root / "Slot1/General.sav"));
	// The new-game save contains the general values the executable resets on continue.
	for(auto so : actors) vm.reset_variables(so, 0);
	for(const auto& e : registry.general)
		if(e.standard_offset == 0x21u) memory.write<std::uint8_t>(e.address, 15);
	auto expected = game::capture_save_entries(memory, registry.general, registry.general_size);
	auto written = save_file(root / "Slot1/General.sav").bytes;
	for(std::size_t i = 0; i < expected.size(); ++i)
	{
		INFO("general byte ", i);
		CHECK(written[i] == expected[i]);
	}
	// Load through the public --save path into another level's independently built registry.
	auto learn = load_level_files(*data, "Learn_10");
	auto learn_header = read_level_header(learn);
	scene learn_tree(learn.memory, learn_header);
	auto learn_registry = read_save_registry(learn.memory, learn_header, learn_tree);
	game::live_memory restored_memory(learn.memory);
	game::interpreter restored(restored_memory, learn_header.main_actor->address());
	game::load_save_slot(restored, root, learn, learn_header, learn_tree, 1);
	CHECK(game::capture_save_entries(restored_memory, learn_registry.general, learn_registry.general_size) == expected);
	// Current-state writing includes scalar, reversed-array and packed always/placed actor bits.
	auto script = restored_memory.allocate(24);
	restored_memory.write<std::uint32_t>(script, 90);
	restored_memory.write<std::uint8_t>(script + 7, std::uint8_t(node_type::function));
	restored_memory.write<std::uint32_t>(script + 8, 1);
	restored_memory.write<std::uint8_t>(script + 14, 1);
	restored_memory.write<std::uint8_t>(script + 15, std::uint8_t(node_type::integer));
	restored.save_folder = root.string();
	game::install_save_handlers(restored, learn, learn_header, learn_tree, *dir, session);
	for(const auto& e : learn_registry.general)
		if(!e.standard_offset) restored_memory.write<std::uint8_t>(e.address, 77);
	for(const auto& e : learn_registry.level) restored_memory.write<std::uint8_t>(e.address, 1);
	game::value result;
	CHECK(restored.eval(learn_header.main_actor->address(), script, result) == script + 16);
	REQUIRE(result.as_int() == 1);
	CHECK(save_file(root / "Slot1/General.sav").bytes == game::capture_save_entries(restored_memory, learn_registry.general, learn_registry.general_size));
	CHECK(save_file(root / "Slot1/Learn_10_0.sav").bytes == game::capture_save_entries(restored_memory, learn_registry.level, learn_registry.level_size));
	// An invalid slot consumes the argument and reports failure without publishing files.
	restored_memory.write<std::uint32_t>(script + 8, 4);
	CHECK(restored.eval(learn_header.main_actor->address(), script, result) == script + 16);
	CHECK(result.as_int() == 0);
	CHECK_FALSE(session->error.empty());
	std::filesystem::remove_all(root);
}

TEST_CASE("real data: Hall of Doors prompt writes the active slot and reports success")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto data = game_data::locate(*dir); REQUIRE(data);
	auto files = load_level_files(*data, "Mapmonde");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	game::live_memory memory(files.memory);
	game::interpreter vm(memory, header.main_actor->address());
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	auto root = game::save_root() / ("hall-test-" + std::to_string(std::random_device{}()));
	game::write_save_slot(root, *dir, 2, {}, {}, "RAY", 0);
	vm.save_folder = root.string();
	bool automatic = false;
	SUBCASE("original prompts") {}
	SUBCASE("autosave") { automatic = true; }
	REQUIRE(game::write_autosave(root, 2, automatic));
	auto session = std::make_shared<game::save_session>(); session->slot = 2;
	game::install_save_handlers(vm, files, header, tree, *dir, session);
	game::input keys(memory, read_input_actions(files));
	auto prompt = tree.node(89).at.address();
	vm.set_variable(prompt, 0, game::value::integer(1));
	vm.set_variable(tree.node(90).at.address(), 1, game::value::integer(0));
	keys.step(); vm.think(prompt);
	CHECK(vm.paused());
	CHECK(vm.get_variable(prompt, 7).as_int() == 1);
	if(automatic)
	{
		// A direction held during arrival can toggle the script's choice to No.
		// Autosave must restore Yes before supplying its confirmation.
		vm.set_variable(prompt, 7, game::value::integer(0));
		CHECK(vm.on_screen_text(prompt, game::value::of(game::value_type::id, 0x190)));
		CHECK(vm.get_variable(prompt, 7).as_int() == 1);
	}
	keys.set_key(0x1C, !automatic); keys.step(); vm.think(prompt);
	INFO(session->error);
	CHECK(vm.get_variable(prompt, 8).as_int() == 1);
	auto registry = read_save_registry(files.memory, header, tree);
	CHECK(save_file(root / "Slot2/General.sav").bytes == game::capture_save_entries(memory, registry.general, registry.general_size));
	CHECK(std::filesystem::exists(root / "Slot2/Mapmonde_0.sav"));
	if(automatic)
	{
		CHECK(session->saving_ticks == 390);
		CHECK_FALSE(vm.screen_text().at(9).used);
		for(unsigned tick = 0; tick < 30; ++tick) { vm.next_tick(); keys.step(); vm.think(prompt); }
		CHECK_FALSE(vm.paused());
		CHECK(session->automatic_actor == 0);
	}
	std::filesystem::remove_all(root);
}

TEST_CASE("autosave preference is asked once, persists both choices and fades the indicator")
{
	auto root = game::save_root() / ("autosave-test-" + std::to_string(std::random_device{}()));
	CHECK_FALSE(game::read_autosave(root, 1));
	game::save_session session;
	session.slot = 1;
	session.question = true;
	game::update_save_ui(session, root, false, true, false);
	CHECK(session.choice);
	game::update_save_ui(session, root, false, false, true);
	CHECK_FALSE(session.question);
	CHECK(session.autosave == true);
	CHECK(game::read_autosave(root, 1) == true);
	// Yes shows the saving Lum with its explanation until Enter is pressed again.
	CHECK(session.explaining);
	for(unsigned tick = 0; tick < 300; ++tick) game::update_save_ui(session, root, false, false, false);
	CHECK(game::saving_alpha(session) == 1);
	game::update_save_ui(session, root, false, false, true);
	CHECK_FALSE(session.explaining);
	game::update_save_ui(session, root, false, false, false);
	CHECK(game::saving_alpha(session) < 1);
	// Per save: another slot keeps its own choice (none yet), and forgetting one leaves the other.
	CHECK_FALSE(game::read_autosave(root, 2));
	REQUIRE(game::write_autosave(root, 2, false));
	CHECK(game::read_autosave(root, 1) == true);
	CHECK(game::read_autosave(root, 2) == false);
	game::forget_autosave(root, 1);
	CHECK_FALSE(game::read_autosave(root, 1));
	CHECK(game::read_autosave(root, 2) == false);
	{ std::ofstream out(root / "autosave_Slot2.txt"); out << "1 garbage"; }
	CHECK_FALSE(game::read_autosave(root, 2));
	session.saving_ticks = 150;
	CHECK(game::saving_alpha(session) == 1);
	for(unsigned tick = 0; tick < 120; ++tick) game::update_save_ui(session, root, false, false, false);
	CHECK(game::saving_alpha(session) == 1);
	for(unsigned tick = 0; tick < 15; ++tick) game::update_save_ui(session, root, false, false, false);
	CHECK(game::saving_alpha(session) == doctest::Approx(0.5));
	for(unsigned tick = 0; tick < 15; ++tick) game::update_save_ui(session, root, false, false, false);
	CHECK(game::saving_alpha(session) == 0);
	std::filesystem::remove_all(root);
}
