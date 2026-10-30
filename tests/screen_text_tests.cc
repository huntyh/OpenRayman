#include <openrayman/game/screen_text.h>

#include <doctest/doctest.h>

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
