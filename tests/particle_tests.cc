#include <openrayman/game/particles.h>

#include <doctest/doctest.h>

using openrayman::game::particles;

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
