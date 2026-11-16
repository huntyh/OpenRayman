#include <openrayman/game/particles.h>

#include <doctest/doctest.h>

#include <cmath>

using openrayman::game::particles;

TEST_CASE("particles: portal segments persist and deduplicate either endpoint order")
{
	particles p;
	const particles::vec3 a{1, 2, 3}, b{4, 5, 6}, c{7, 8, 9};
	p.start(163, a, b, 42, 1, 0);
	p.start(163, b, a, 42, 1, 1);
	REQUIRE(p.all().size() == 1);
	CHECK(particles::is_segment(p.all()[0]));
	CHECK(p.all()[0].position == a);
	CHECK(p.all()[0].end == b);
	p.start(163, b, c, 42, 1, 1);
	p.update(10, 1);
	p.set_paused(true);
	p.set_paused(false);
	CHECK(p.all().size() == 2);
}

TEST_CASE("particles: screen rectangle lasts one tick and keeps its coordinates and opacity")
{
	particles p;
	p.start(110, {14, 80.6f, 0}, {94, 92.4f, 0}, 0x1234, 180, 1);
	p.update(1, 0.016);
	REQUIRE(p.screen_rectangles().size() == 1);
	CHECK(p.screen_rectangles()[0].first[0] == doctest::Approx(14));
	CHECK(p.screen_rectangles()[0].second[1] == doctest::Approx(92.4));
	CHECK(p.screen_rectangles()[0].alpha == doctest::Approx(180));
	CHECK(p.all().empty());
	p.begin_tick();
	p.update(1.016, 0.016);
	CHECK(p.screen_rectangles().empty());
}

TEST_CASE("particles: one-draw rectangle expires across a paused clock rewind")
{
	particles p;
	p.begin_tick();
	p.start(110, {0, 0, 0}, {100, 100, 0}, 1, 254, 3);
	p.update(0.05, 0.016); // No resumes the clock after submitting this cover.
	REQUIRE(p.screen_rectangles().size() == 1);
	p.begin_tick();
	p.update(0.066, 0.016);
	CHECK(p.screen_rectangles().empty());
	// A paused clock need not advance for a one-draw rectangle to be retired.
	p.start(110, {0, 0, 0}, {100, 100, 0}, 1, 254, 0.066);
	p.begin_tick();
	p.update(0.066, 0.016);
	CHECK(p.screen_rectangles().empty());
}

TEST_CASE("particles: the actor that draws a gauge is the HUD's")
{
	// Ours (docs/formats/text.md): gauges are edge HUD, and so is what their actor draws.
	particles p;
	p.start(120, {12, 8.5f, 60}, {16.6f, 11.5f, 200}, 0xBBBB, 0, 1, 0x4400);
	p.start(125, {2, 4, 0}, {11, 15.5f, 200}, 0x1234, 0, 1, 0x4400); // Rayman's face
	p.start(125, {4, 80, 0}, {14, 95, 192}, 0x1234, 5, 1, 0x4200);   // Murfy's portrait
	CHECK(p.draws_gauge(0x4400));
	CHECK_FALSE(p.draws_gauge(0x4200));
	CHECK_FALSE(p.draws_gauge(0));
	REQUIRE(p.screen_images().size() == 2);
	CHECK(p.screen_images()[0].owner == 0x4400);
	CHECK(p.screen_images()[1].owner == 0x4200);
	for(const auto& piece : p.screen_bar_images()) CHECK(piece.edge);
	// A hidden gauge still names its actor.
	p.start(120, {12, 8.5f, 60}, {16.6f, 11.5f, 0}, 0xBBBB, 0, 1, 0x4400);
	CHECK(p.draws_gauge(0x4400));
}

TEST_CASE("particles: screen images use the script slot and opacity")
{
	particles p;
	p.start(125, {4, 80, 0}, {14, 95, 192}, 0x1234, 5, 1);
	p.update(1, 0.016);
	REQUIRE(p.screen_images().size() == 1);
	CHECK(p.screen_images()[0].slot == 5);
	CHECK(p.screen_images()[0].second[2] == doctest::Approx(192));
	p.update(2, 0.016);
	CHECK(p.screen_images().size() == 1);
	p.start(125, {4, 80, 0}, {14, 95, 0}, 0x1234, 5, 1);
	CHECK(p.screen_images().empty());
}


TEST_CASE("particles: a gauge fills from its left with kind 50's track behind the rest")
{
	// Node 44's health bar: track material from kind 50, then the bar in slot 0, 60 percent full.
	particles p;
	p.start(50, {0, 0, 0}, {0, 0, 0}, 0xAAAA, 0, 1);
	p.start(120, {12, 8.5f, 60}, {16.6f, 11.5f, 200}, 0xBBBB, 0, 1);
	auto images = p.screen_bar_images();
	REQUIRE(images.size() == 2);
	CHECK(images[0].material == 0xBBBB);
	CHECK(images[0].first[0] == doctest::Approx(12));
	CHECK(images[0].second[0] == doctest::Approx(12 + 4.6f * 0.6f));
	CHECK(images[0].second[2] == doctest::Approx(200));
	CHECK(images[1].material == 0xAAAA);
	CHECK(images[1].second[0] == doctest::Approx(16.6f));
	// Full: no track; the same slot replaces the bar; opacity 0 hides it.
	p.start(120, {12, 8.5f, 100}, {16.6f, 11.5f, 200}, 0xBBBB, 0, 1);
	CHECK(p.screen_bar_images().size() == 1);
	p.start(120, {12, 8.5f, 100}, {16.6f, 11.5f, 0}, 0xBBBB, 0, 1);
	CHECK(p.screen_bar_images().empty());
}

TEST_CASE("particles: kinds 170/171 lay out the tiled logo in six columns of three")
{
	// Jail_10's Rayman 2 logo: 18 tiles, then the corners (15, 20)-(85, 60) and an opacity.
	particles p;
	for(int i = 0; i < 18; i++)
		p.start(170, {0, 0, 0}, {0, 0, 0}, 0x1000u + std::uint32_t(i), float(i), 1);
	CHECK(p.screen_tile_images().empty()); // no opacity yet
	p.start(171, {15, 20, 0}, {85, 60, 0}, 0, 128, 1);
	auto tiles = p.screen_tile_images();
	REQUIRE(tiles.size() == 18);
	// Tile 4 is column 1, row 1 (logo_11).
	const auto& t = tiles[4];
	CHECK(t.material == 0x1004);
	CHECK(t.first[0] == doctest::Approx(15 + 70.0f / 6));
	CHECK(t.first[1] == doctest::Approx(20 + 40.0f / 3));
	CHECK(t.second[0] == doctest::Approx(15 + 2 * 70.0f / 6));
	CHECK(t.second[2] == doctest::Approx(128));
	CHECK(!t.edge);
	p.start(171, {15, 20, 0}, {85, 60, 0}, 0, 0, 1);
	CHECK(p.screen_tile_images().empty());
}

TEST_CASE("particles: a breaking cage's golden sparks and smoke")
{
	particles p;
	p.start(45, {0, 0, 0}, {0, 0, 0}, 0x1234, 2, 1);
	REQUIRE(p.all().size() == 9);
	for(const auto& spark : p.all())
	{
		CHECK(spark.flags == 0x2501);
		CHECK(std::abs(spark.motion[0]) <= 4);
		CHECK(spark.motion[2] >= -0.4f);
		CHECK(spark.motion[2] <= 12);
		CHECK(spark.size >= 0.32f);
		CHECK(spark.size <= 1.6f);
	}
	p.start(5, {0, 0, 0}, {0, 0, 0}, 0x1234, 1, 1);
	REQUIRE(p.all().size() == 13);
	CHECK(p.all()[9].flags == 0x1103);
	CHECK(p.all()[9].position[2] == doctest::Approx(1.4f));
	CHECK(p.all()[10].height == doctest::Approx(0.6f));
}

TEST_CASE("particles: rising specks come at most every 0.05 s and fade out in about 2.2 s")
{
	particles p;
	const particles::vec3 camera{ 10, 20, 30 }, forward{ 0, -1, 0 };
	double now = 0;
	// The scripts ask every tick for a second: 20 specks, not 60.
	for(int tick = 0; tick < 60; tick++, now += 1.0 / 60)
	{
		p.start(28, camera, forward, 0x1234, 0.5f, now);
		p.update(now, 1.0 / 60);
	}
	CHECK(p.all().size() == 20);
	for(const auto& speck : p.all())
	{
		CHECK(speck.material == 0x1234);
		CHECK(speck.size >= 0.1f);
		CHECK(speck.size <= 0.125f);
		CHECK(speck.motion[2] >= 2.5f);
	}
	// Without new ones, the last speck is gone 2.2 s after it started.
	for(int tick = 0; tick < 140; tick++, now += 1.0 / 60)
		p.update(now, 1.0 / 60);
	CHECK(p.all().empty());
}

TEST_CASE("particles: a twinkle glides into its point and vanishes")
{
	particles p;
	p.start(23, { 1, 2, 3 }, { 0, 1, 0 }, 0x1234, 4.5f, 0);
	REQUIRE(p.all().size() == 1);
	double now = 0;
	for(int tick = 0; tick < 60 && !p.all().empty(); tick++)
		p.update(now += 1.0 / 60, 1.0 / 60);
	CHECK(p.all().empty());
}

TEST_CASE("barrel trail emits and moves eight fading sprites")
{
	particles p;
	p.start(15, { 1, 2, 3 }, { 0, 1, 0 }, 123, 4, 0);
	REQUIRE(p.all().size() == 8);
	const auto first = p.all()[0];
	CHECK(first.type == 9);
	CHECK(first.material == 123);
	CHECK(first.motion[1] >= 2);
	CHECK(first.motion[1] <= 8.8f);
	p.update(0.1, 0.1);
	REQUIRE(p.all().size() == 8);
	CHECK(p.all()[0].motion[0] == doctest::Approx(-0.16));
	CHECK(p.all()[0].position[1] > first.position[1]);
	CHECK(p.all()[0].alpha < first.alpha);
}
