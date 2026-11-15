#include <openrayman/game/mechanics.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/game/interpreter.h>
#include <doctest/doctest.h>
#include <cmath>

using namespace openrayman;

namespace
{
	collision_triangle floor_triangle(float z = 0)
	{
		collision_triangle t;
		t.corners = { vec3{ -4, -4, z }, vec3{ 4, -4, z }, vec3{ 0, 4, z } };
		t.normal = { 0, 0, 1 };
		t.material = 123;
		return t;
	}

	struct projectile
	{
		game::live_memory m{ memory_image{} };
		std::uint32_t so, actor, transform, base, report, set, sphere;
		projectile()
		{
			m.allocate(16);
			so = m.allocate(0x48); actor = m.allocate(0x38); transform = m.allocate(0x58);
			auto dynamics = m.allocate(4);
			base = m.allocate(0x1DC); report = m.allocate(0x16C); set = m.allocate(0x48);
			auto head = m.allocate(12), node = m.allocate(12), zone = m.allocate(0x34);
			auto vertices = m.allocate(12), kinds = m.allocate(2), data = m.allocate(4), list = m.allocate(8);
			sphere = m.allocate(12);
			m.write<std::uint32_t>(so, 2); m.write(so + 4, actor); m.write(so + 0x24, transform);
			m.write(actor + 8, dynamics); m.write(dynamics, base); m.write(actor + 0x14, set);
			m.write(set + 8, head); m.write(head, node); m.write(node + 4, zone);
			m.write<std::uint32_t>(set + 0x38, 1); // force kind-1 zone 0 on
			m.write(zone, vertices); m.write(zone + 0x10, kinds); m.write(zone + 0x14, data);
			m.write<std::uint16_t>(zone + 0x2E, 1); m.write<std::uint16_t>(kinds, 7);
			m.write(data, list); m.write(list, sphere); m.write<std::uint16_t>(list + 4, 1);
			m.write<float>(sphere, 0.25f);
			// The centre is above the actor origin, so stopping the origin on the plane is wrong.
			m.write(vertices, vec3{ 0, 0, 0.5f });
			m.write<std::uint32_t>(base + 8, 2 | 0xE4000);
			m.write<std::uint32_t>(base + 0xC, 2 | 0x4000);
			m.write(base + 0x150, report);
			affine pose; pose.m[2][3] = 4;
			game::set_global_placement(m, so, pose);
			m.write(base + 0x7C, vec3{ 0, 0, 4 });
			m.write(base + 0x24, vec3{ 0, 0, -12 });
		}
		void move(const game::world_collision& world) { game::move_actor(m, so, actor, 1000, world); }
	};
}

TEST_CASE("sphere sweep hits the first thin floor along a diagonal move")
{
	auto top = floor_triangle(2), bottom = floor_triangle(-2);
	game::world_collision world({ bottom, top }); // deliberately reverse contact order
	auto hit = world.sweep_sphere({ -1, 0, 5 }, { 1, 0, -5 }, 0.25f);
	REQUIRE(hit);
	CHECK(hit->fraction == doctest::Approx(0.275f));
	CHECK(hit->point[2] == doctest::Approx(2));
	CHECK(hit->point[0] == doctest::Approx(-0.45f));
	CHECK(hit->normal == vec3{ 0, 0, 1 });
	CHECK(hit->material == 123);
	CHECK_FALSE(world.sweep_sphere({ 0, 0, 2.25f }, { 0, 0, 5 }, 0.25f));
	CHECK_FALSE(world.sweep_sphere({ 0, 0, 5 }, { 0, 0, 5 }, 0.25f));
	auto point = world.sweep_sphere({ 0, 0, 5 }, { 0, 0, -5 }, 0);
	REQUIRE(point);
	CHECK(point->fraction == doctest::Approx(0.3f));
}

TEST_CASE("sphere sweep catches triangle edges and vertices outside the face")
{
	auto t = floor_triangle();
	t.corners = { vec3{ 0, 0, 0 }, vec3{ 4, 0, 0 }, vec3{ 0, 4, 0 } };
	game::world_collision world({ t });
	auto edge = world.sweep_sphere({ 2, -0.3f, 2 }, { 2, -0.3f, -2 }, 0.5f);
	REQUIRE(edge);
	CHECK(edge->fraction == doctest::Approx(0.4f));
	CHECK(edge->point == vec3{ 2, 0, 0 });
	auto vertex = world.sweep_sphere({ -0.3f, -0.3f, 2 }, { -0.3f, -0.3f, -2 }, 0.5f);
	REQUIRE(vertex);
	CHECK(vertex->point == vec3{ 0, 0, 0 });
	CHECK(vertex->fraction == doctest::Approx((2 - std::sqrt(0.07f)) / 4));
	CHECK_FALSE(world.sweep_sphere({ -0.6f, -0.6f, 2 }, { -0.6f, -0.6f, -2 }, 0.5f));
}

TEST_CASE("sphere sweep respects sectors, pass-through flags and actor surface ownership")
{
	auto solid = floor_triangle(); solid.sector = 10; solid.node = 456;
	auto water = floor_triangle(2); water.flags = surface::water;
	auto pass = floor_triangle(3); pass.flags = 0x8000;
	game::world_collision world({ solid, water, pass });
	const std::uint32_t excluded[] = { 20 };
	CHECK_FALSE(world.in_sectors(excluded).sweep_sphere({ 0, 0, 5 }, { 0, 0, -5 }, 0.1f));
	auto hit = world.sweep_sphere({ 0, 0, 5 }, { 0, 0, -5 }, 0.1f);
	REQUIRE(hit); CHECK(hit->point[2] == doctest::Approx(0)); CHECK(hit->actor == 0);
	game::world_collision actors({ solid }, true);
	hit = actors.sweep_sphere({ 0, 0, 5 }, { 0, 0, -5 }, 0.1f);
	REQUIRE(hit); CHECK(hit->actor == 456);
}

TEST_CASE("basic actor crossing a thin floor stops its offset sphere and reports contact")
{
	projectile p;
	p.m.write<std::uint32_t>(p.sphere + 4, 234);
	p.move(game::world_collision({ floor_triangle() }));
	CHECK(p.m.f32(p.transform + 12) == doctest::Approx(-0.25f));
	CHECK(p.m.f32(p.base + 0x44) == doctest::Approx(0));
	CHECK(p.m.u32(p.report + 4) == 1);
	CHECK(p.m.f32(p.report + 8 + 0x10 + 8) == doctest::Approx(0));
	CHECK(p.m.u32(p.report + 8 + 0x20) == 123);
	CHECK(p.m.u32(p.report + 8 + 0x1C) == 234);
	CHECK(p.m.u32(p.report + 8 + 0x28 + 0x20) == 123);
}

TEST_CASE("basic actor sweep includes receiving actor surfaces along a long move")
{
	projectile p;
	auto& m = p.m;
	auto owner = m.allocate(0x48), actor = m.allocate(0x38), standard = m.allocate(0x34), transform = m.allocate(0x58);
	m.write<std::uint32_t>(owner, 2); m.write(owner + 4, actor); m.write(owner + 0x24, transform);
	m.write(actor + 4, standard); m.write(standard + 0xC, owner);
	affine pose; pose.m[0][3] = 20;
	game::set_global_placement(m, owner, pose);
	auto set = m.allocate(0x48), head = m.allocate(12), node = m.allocate(8), zone = m.allocate(0x30);
	auto vertices = m.allocate(36), kinds = m.allocate(2), elements = m.allocate(4), element = m.allocate(0x14), indices = m.allocate(6);
	m.write(actor + 0x14, set); m.write(set + 0xC, head); m.write(head, node); m.write(node + 4, zone);
	m.write<std::uint32_t>(set + 0x3C, 1);
	m.write(zone, vertices); m.write(zone + 0x10, kinds); m.write(zone + 0x14, elements);
	m.write<std::uint16_t>(zone + 0x2C, 3); m.write<std::uint16_t>(zone + 0x2E, 1);
	m.write<std::uint16_t>(kinds, 1); m.write(elements, element);
	m.write<std::uint16_t>(element + 4, 1); m.write(element + 8, indices);
	const vec3 corners[] = { {0,-5,-5}, {0,0,8}, {0,5,-5} };
	for(std::uint32_t i = 0; i < 3; i++) { m.write(vertices + i * 12, corners[i]); m.write<std::uint16_t>(indices + i * 2, std::uint16_t(i)); }
	m.write(p.transform + 4, vec3{});
	m.write(p.base + 0x24, vec3{40,0,0});
	game::interpreter vm(m, p.so); vm.set_actors({ p.so, owner });
	CHECK(game::actor_collision_triangles(vm, p.so).empty()); // outside old point-based reach
	game::move_actor(m, p.so, p.actor, 1000, {}, &vm);
	CHECK(m.f32(p.transform + 4) == doctest::Approx(19.75f));
	CHECK(m.u32(p.report + 4) == 4);
	CHECK(m.u32(p.report + 8 + 0x24) == owner);
}

TEST_CASE("basic actor sweep selects all spheres in the state group")
{
	projectile p;
	auto& m = p.m;
	auto head = m.u32(p.set + 8), node = m.u32(head), zone = m.u32(node + 4), list = m.u32(m.u32(zone + 0x14));
	auto vertices = m.allocate(24), spheres = m.allocate(24), group = m.allocate(12), link = m.allocate(8);
	m.write(vertices, vec3{0,0,0.5f}); m.write(vertices + 12, vec3{0,0,-1});
	m.write(zone, vertices); m.write(list, spheres); m.write<std::uint16_t>(list + 4, 2);
	m.write<float>(spheres, 0.25f); m.write<float>(spheres + 12, 0.5f);
	m.write<std::uint16_t>(spheres + 12 + 8, 1);
	m.write<std::uint32_t>(p.set + 0x38, 0); m.write(p.set + 0x2C, group);
	m.write(group, link); m.write<std::uint32_t>(group + 8, 1); // group selects zone 0
	p.move(game::world_collision({floor_triangle()}));
	CHECK(m.f32(p.transform + 12) == doctest::Approx(1.5f)); // the second sphere arrives first
	CHECK(m.u32(p.report + 4) == 1);
}

TEST_CASE("basic actor sweep reports walls and ceilings and clears old contact")
{
	projectile p;
	auto t = floor_triangle();
	SUBCASE("wall")
	{
		t.corners = { vec3{ 0, -5, -5 }, vec3{ 0, 5, -5 }, vec3{ 0, 0, 8 } };
		t.normal = { -1, 0, 0 };
		p.m.write(p.transform + 4, vec3{ -3, 0, 0 });
		p.m.write(p.base + 0x24, vec3{ 10, 0, 0 });
		p.move(game::world_collision({ t }));
		CHECK(p.m.f32(p.transform + 4) == doctest::Approx(-0.25f));
		CHECK(p.m.u32(p.report + 4) == 4);
		CHECK(p.m.u32(p.report + 8 + 2 * 0x28 + 0x20) == 123);
	}
	SUBCASE("ceiling")
	{
		t.normal = { 0, 0, -1 };
		p.m.write(p.transform + 12, -4.0f);
		p.m.write(p.base + 0x24, vec3{ 0, 0, 10 });
		p.move(game::world_collision({ t }));
		CHECK(p.m.f32(p.transform + 12) == doctest::Approx(-0.75f));
		CHECK(p.m.u32(p.report + 4) == 0x10);
		CHECK(p.m.u32(p.report + 8 + 5 * 0x28 + 0x20) == 123);
	}
	p.m.write(p.base + 8, std::uint32_t(2 | 0xE4000));
	p.m.write(p.base + 0x24, vec3{ 1, 0, 0 });
	p.move({});
	CHECK(p.m.u32(p.report + 4) == 0);
	CHECK(p.m.u32(p.report + 8 + 0x20) == 0);
}

TEST_CASE("disabled zones and non-colliding fast projectiles pass through scenery")
{
	projectile p;
	SUBCASE("disabled zone") { p.m.write<std::uint32_t>(p.set + 0x38, 2); }
	SUBCASE("fast projectile without collision bit")
	{
		p.m.write<std::uint32_t>(p.base + 0xC, 0x4202);
		p.m.write<std::uint32_t>(p.base + 8, 0xE4000);
	}
	SUBCASE("fast projectile with collision bit")
	{
		p.m.write<std::uint32_t>(p.base + 0xC, 0x4202);
		p.move(game::world_collision({ floor_triangle() }));
		CHECK(p.m.f32(p.transform + 12) == doctest::Approx(-0.25f));
		CHECK(p.m.u32(p.report + 4) == 1);
		return;
	}
	p.move(game::world_collision({ floor_triangle() }));
	CHECK(p.m.f32(p.transform + 12) == doctest::Approx(-8));
	CHECK(p.m.u32(p.report + 4) == 0);
}
