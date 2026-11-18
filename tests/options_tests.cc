#include <openrayman/game/options.h>
#include <doctest/doctest.h>
#include <fstream>
#include <random>

using namespace openrayman::game;

TEST_CASE("engine config round trip, missing and malformed settings")
{
	const auto root = std::filesystem::temp_directory_path() / ("openrayman_options_" + std::to_string(std::random_device{}()));
	REQUIRE_FALSE(std::filesystem::exists(root));
	CHECK(read_options(root) == engine_options{});
	engine_options expected;
	expected.music = 3; expected.effects = 0; expected.fullscreen = true;
	expected.vsync = 2; expected.fps = 144; expected.loading_screens = false; expected.language = 4;
	REQUIRE(write_options(root, expected));
	CHECK(read_options(root) == expected);
	expected.music = 9;
	REQUIRE(write_options(root, expected));
	CHECK(read_options(root) == expected);
	{
		std::ofstream out(root / "openrayman.cfg");
		out << "music=-1\neffects=11\nvsync=3\nfps_limit=garbage\nlanguage=5\nfullscreen=2\nloading_screens=0 trailing\nunknown=1\nmusic=6\n";
	}
	engine_options valid; valid.music = 6;
	CHECK(read_options(root) == valid);
	// A file in place of the directory must report failure.
	CHECK_FALSE(write_options(root / "openrayman.cfg", expected));
	std::filesystem::remove_all(root);
}

TEST_CASE("Options navigation wraps, edits only with arrows and backs out one page")
{
	options_page ui; ui.active = true;
	engine_options values;
	bool autosave = false;
	using action = options_page::action;
	auto press = [&](action a) { return ui.navigate(a, values, autosave, false); };
	press(action::up); CHECK(ui.selected == 3);
	press(action::down); CHECK(ui.selected == 0);
	press(action::confirm); CHECK(ui.current == options_page::page::sound);
	CHECK(press(action::left)); CHECK(values.music == 9);
	CHECK_FALSE(press(action::confirm)); CHECK(values.music == 9);
	for(int i = 0; i < 20; ++i) press(action::left);
	CHECK(values.music == 0);
	press(action::back); CHECK(ui.current == options_page::page::root); CHECK(ui.active);
	press(action::down); press(action::confirm);
	CHECK(ui.current == options_page::page::display);
	CHECK(press(action::right)); CHECK(values.fullscreen);
	press(action::down); press(action::left); CHECK(values.vsync == 2);
	press(action::back); press(action::down); press(action::confirm);
	CHECK(ui.current == options_page::page::game);
	press(action::down);
	CHECK_FALSE(press(action::right)); CHECK_FALSE(autosave);
	CHECK(ui.navigate(action::right, values, autosave, true)); CHECK(autosave);
	screen_text slots;
	ui.draw(slots, values, autosave, 2);
	CHECK(slots.at(2).wobble == 1);
	CHECK(slots.at(2).colour == 3);
	CHECK(slots.at(2).text.find("On") != std::string::npos);
	press(action::back); press(action::back); CHECK_FALSE(ui.active);
}
