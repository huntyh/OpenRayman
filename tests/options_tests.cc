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
	expected.music = 3; expected.effects = 0; expected.fullscreen = 2; expected.display_width = 1920; expected.display_height = 1080; expected.refresh_millihz = 59940;
	expected.vsync = 2; expected.fps = 144; expected.loading_screens = false; expected.visual_improvements = false; expected.language = 4;
	REQUIRE(write_options(root, expected));
	CHECK(read_options(root) == expected);
	expected.music = 9;
	REQUIRE(write_options(root, expected));
	CHECK(read_options(root) == expected);
	{
		std::ofstream out(root / "openrayman.cfg");
		out << "music=-1\neffects=11\nvsync=3\nfps_limit=garbage\nlanguage=5\nfullscreen=3\nloading_screens=0 trailing\nunknown=1\nmusic=6\n";
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
	press(action::down); press(action::down); press(action::down);
	CHECK(press(action::left)); CHECK_FALSE(values.visual_improvements);
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

TEST_CASE("Exclusive mode row follows fullscreen choice and display modes round trip")
{
 options_page ui; ui.active = true; ui.current = options_page::page::display;
 engine_options v; bool autosave = false;
 ui.modes = {{1280,720,60000}, {1920,1080,59940}};
 CHECK(ui.entries(v).size() == 6);
 ui.navigate(options_page::action::right, v, autosave, false); CHECK(v.fullscreen == 1);
 ui.navigate(options_page::action::right, v, autosave, false); CHECK(v.fullscreen == 2);
 CHECK(ui.entries(v).size() == 7);
 ui.selected = 1;
 CHECK(ui.navigate(options_page::action::right, v, autosave, false));
 CHECK(v.display_width == 1920); CHECK(v.refresh_millihz == 59940);
 ui.desktop_display = false; CHECK(ui.entries(v).size() == 5);
}

TEST_CASE("Fullscreen config accepts all three choices and legacy on means borderless")
{
 const auto root=std::filesystem::temp_directory_path()/("display-config-"+std::to_string(std::random_device{}()));
 for(int mode : {0,1,2}) { engine_options v; v.fullscreen=mode; REQUIRE(write_options(root,v)); CHECK(read_options(root)==v); }
 std::filesystem::remove_all(root);
}

TEST_CASE("Language activates with Enter and wraps with either arrow")
{
	options_page page; page.active=true; page.current=options_page::page::game;
	engine_options options; options.language=0; bool autosave=false;
	CHECK(page.navigate(options_page::action::confirm,options,autosave,false)); CHECK(options.language==1);
	CHECK(page.navigate(options_page::action::left,options,autosave,false)); CHECK(options.language==0);
	CHECK(page.navigate(options_page::action::left,options,autosave,false)); CHECK(options.language==4);
}
