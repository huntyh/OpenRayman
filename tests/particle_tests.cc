#include <openrayman/game/particles.h>

#include <doctest/doctest.h>

using openrayman::game::particles;

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
	p.update(1.016, 0.016);
	CHECK(p.screen_rectangles().empty());
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
