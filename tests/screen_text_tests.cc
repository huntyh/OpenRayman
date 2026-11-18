#include <openrayman/game/screen_text.h>
#include "../src/audio/music_fade.h"
#include <openrayman/game/sound_placement.h>

#include <doctest/doctest.h>
#include <openrayman/game/dialog.h>

TEST_CASE("screen text preserves the size from the script position")
{
	openrayman::game::screen_text text;
	REQUIRE(text.set(15, 690, 70, "1;5", 255, 18));
	CHECK(text.at(15).size == doctest::Approx(18));
}

using openrayman::game::screen_text;

TEST_CASE("screen text: 505 sets a slot up and 507 types into it one character at a time")
{
	screen_text text;
	REQUIRE(text.set(17, 690, 180, "New game", 255));
	const auto& s = text.at(17);
	CHECK(s.used);
	CHECK(s.text == "New game");
	CHECK(s.x == doctest::Approx(690));
	CHECK(s.y == doctest::Approx(180));
	CHECK(s.alpha == 255);
	CHECK(s.size == doctest::Approx(0));

	// The menu's loop: overwrite index by index, as its scripts do.
	REQUIRE(text.put_char(17, 0, 'N'));
	REQUIRE(text.put_char(17, 1, 'e'));
	CHECK(text.at(17).text.size() == 8);
}

TEST_CASE("screen text: 507 does nothing to a slot that was never set")
{
	screen_text text;
	// The exe checks the slot's text pointer first, so a stray write is dropped.
	CHECK_FALSE(text.put_char(3, 0, 'x'));
	CHECK_FALSE(text.at(3).used);
	CHECK(text.used() == 0);
}

TEST_CASE("screen text: 506 sets the fields the menu toggles")
{
	screen_text text;
	REQUIRE(text.set(16, 0, 0, "0", 0));
	CHECK(text.at(16).alpha == 0);

	// what 0: the alpha.
	REQUIRE(text.set_field(16, 0, 200));
	CHECK(text.at(16).alpha == 200);
	// what 2: assigns the whole flag byte to 1, and forces a visible alpha when there is none.
	REQUIRE(text.set_field(16, 0, 0));
	REQUIRE(text.set_field(16, 2, 0));
	CHECK(text.at(16).flags == 1);
	CHECK(text.at(16).alpha == 1);
	// what 3: assigns 2 the same way, leaving the alpha alone.
	REQUIRE(text.set_field(16, 0, 99));
	REQUIRE(text.set_field(16, 3, 0));
	CHECK(text.at(16).flags == 2);
	CHECK(text.at(16).alpha == 99);
	// what 1: the wobble flag at +0x12, not the flash bit.
	REQUIRE(text.set_field(16, 1, 1));
	CHECK(text.at(16).wobble == 1);
	REQUIRE(text.set_field(16, 1, 0));
	CHECK(text.at(16).wobble == 0);
	// what 5: the flash flag and its word index, and only that bit moves.
	REQUIRE(text.set_field(16, 5, 7));
	CHECK(text.at(16).word == 7);
	CHECK((text.at(16).flags & screen_text::flash_word) != 0);
	REQUIRE(text.set_field(16, 5, 0));
	CHECK((text.at(16).flags & screen_text::flash_word) == 0);
	// what 6: the shake bit is only set by exactly 1.0, as the exe's float compare does.
	REQUIRE(text.set_field(16, 6, 1));
	CHECK((text.at(16).flags & screen_text::shake) != 0);
	REQUIRE(text.set_field(16, 6, 0.5f));
	CHECK((text.at(16).flags & screen_text::shake) == 0);
}

TEST_CASE("screen text: the table has 50 slots and out-of-range indices are ignored")
{
	screen_text text;
	CHECK(screen_text::slot_count == 50);
	CHECK(text.all().size() == 50);
	// 0x32 is the exe's bound: index 49 is the last usable one.
	CHECK(text.set(49, 1, 2, "last", 255));
	CHECK_FALSE(text.set(50, 1, 2, "past the end", 255));
	CHECK_FALSE(text.set_field(50, 0, 1));
	CHECK_FALSE(text.put_char(50, 0, 'x'));
	CHECK(text.used() == 1);

	text.clear();
	CHECK(text.used() == 0);
	CHECK_FALSE(text.at(49).used);
}

TEST_CASE("screen slot size and punctuation follow the executable")
{
	using namespace openrayman::game;
	CHECK_FALSE(screen_slot_glyph_index(0x1f));
	CHECK(screen_slot_glyph_index('a') == 33);
	CHECK(screen_slot_glyph_index(0xa0) == 128);
	CHECK(screen_slot_glyph_index(0xa1, 200) == 297);
	auto m = screen_slot_metrics(16);
	CHECK(m.advance == 34);
	CHECK(m.height == 40);
	CHECK(m.line_height == 50);
	CHECK(m.space == 20);
	CHECK(screen_slot_punctuation_offset('.', m.height) == 10);
	CHECK(screen_slot_punctuation_offset(',', m.height) == 20);
	CHECK(screen_slot_punctuation_offset(39, m.height) == -10);
	CHECK(screen_slot_punctuation_offset('A', m.height) == 0);
	// User's 2026-11-07 live recording: HUD size 18, intro counter size 20.
	auto hud = screen_slot_metrics(18);
	CHECK(hud.advance == 38); CHECK(hud.height == 45); CHECK(hud.line_height == 58);
	auto intro = screen_slot_metrics(20);
	CHECK(intro.advance == 42); CHECK(intro.height == 50); CHECK(intro.line_height == 66);
}

TEST_CASE("sound distance fade holds its endpoint instead of muting the intro counter")
{
	using openrayman::game::sound_distance_volume;
	CHECK(sound_distance_volume(0) == 127);
	CHECK(sound_distance_volume(8) == 127);
	CHECK(sound_distance_volume(16) == 108);
	CHECK(sound_distance_volume(30) == 77);
	CHECK(sound_distance_volume(64) == 77);
	CHECK(sound_distance_volume(1000) == 77);
	CHECK(sound_distance_volume(64, 8, 64) == 0);
	CHECK(sound_distance_volume(1000, 8, 4) == 0);
}

TEST_CASE("screen slot alpha ramps move 200 a second, truncated like the executable")
{
	using namespace openrayman::game;
	screen_text text;
	REQUIRE(text.set(0, 0, 0, "fade", 0));
	// Mode 2 (what 2) starts the ramp in and lifts a zero alpha to 1 so the slot is not skipped.
	text.set_field(0, 2, 0);
	CHECK(text.at(0).alpha == 1);
	text.step(1.0f / 60);
	CHECK(text.at(0).alpha == 4); // 1 + 3.33, truncated
	for(int i = 0; i < 200; i++) text.step(1.0f / 60);
	CHECK(text.at(0).alpha == 255);
	CHECK((text.at(0).flags & screen_text::alpha_in) == 0);
	text.set_field(0, 3, 0);
	text.step(1.0f / 60);
	CHECK(text.at(0).alpha == 251); // 255 - 3.33, truncated
	for(int i = 0; i < 200; i++) text.step(1.0f / 60);
	CHECK(text.at(0).alpha == 0);
	CHECK((text.at(0).flags & screen_text::alpha_out) == 0);
	// A slot at alpha 0 is skipped: a ramp in set without mode 2's lift does nothing.
	text.set_field(0, 0, 0);
	text.step(1.0f / 60);
	CHECK(text.at(0).alpha == 0);
}

TEST_CASE("music transition fade lasts one second at the mixer rate")
{
	using namespace openrayman;
	CHECK(music_fade_frames == 22050);
	CHECK(music_fade_gain(music_fade_frames) == doctest::Approx(1.0f));
	CHECK(music_fade_gain(music_fade_frames / 2) == doctest::Approx(0.5f));
	CHECK(music_fade_gain(0) == doctest::Approx(0.0f));
}

TEST_CASE("Menu name keyboard filtering is ASCII uppercase and three cells")
{
 using openrayman::game::filter_menu_name;
 CHECK(filter_menu_name("a9z") == "A9Z");
 CHECK(filter_menu_name("abcDEF") == "ABC");
 CHECK(filter_menu_name("a /:!z") == "AZ");
 CHECK(filter_menu_name("012345") == "012");
 CHECK(filter_menu_name("\xC3\xA9\xE4\xB8\xAD") == "");
 CHECK(filter_menu_name("ab", 1) == "A");
 CHECK(filter_menu_name("a", 0) == "");
}

TEST_CASE("Menu hit testing follows slot layout at 4:3 and 16:9 and rejects gaps")
{
 using namespace openrayman::game;
 screen_text slots; slots.set(0,500,400,"/C:ABC",255,15);
 for(int width : {960,1280}) {
  auto r=screen_slot_rectangle(slots.at(0),width,720);
  CHECK(r.width==doctest::Approx(92.16)); CHECK(r.x+r.width/2==doctest::Approx(width/2));
  CHECK(r.y==doctest::Approx(288)); CHECK(r.height==doctest::Approx(27.36));
  std::vector<menu_target> targets{{r,7}};
  auto left=hit_test_menu(targets,r.x+1,r.y+1), right=hit_test_menu(targets,r.x+r.width-1,r.y+1);
  REQUIRE(left); REQUIRE(right); CHECK_FALSE(left->right); CHECK(right->right); CHECK(left->target==0);
  CHECK_FALSE(hit_test_menu(targets,r.x-1,r.y)); CHECK_FALSE(hit_test_menu(targets,r.x+r.width,r.y));
  CHECK_FALSE(hit_test_menu(targets,r.x,r.y+r.height+1));
 }
 slots.set_field(0,0,0); CHECK(screen_slot_rectangle(slots.at(0),1280,720).width==0);
 slots.set(0,100,400,"/C:ABC/O200:DEF",255,15); auto r=screen_slot_rectangle(slots.at(0),960,720);
 CHECK(r.x+r.width/2==doctest::Approx(480)); CHECK(r.width==doctest::Approx(184.32));
}
