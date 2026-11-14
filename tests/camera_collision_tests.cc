#include <openrayman/game/mechanics.h>
#include <openrayman/game/camera.h>

#include <doctest/doctest.h>

#include <cmath>

using namespace openrayman;

TEST_CASE("collision query views exclude scenery outside the collision sector list")
{
	collision_triangle ground, decorative, wall, water;
	ground.corners = { vec3{ -5, -5, 0 }, vec3{ 5, -5, 0 }, vec3{ 0, 5, 0 } };
	ground.normal = { 0, 0, 1 }; ground.sector = 10;
	decorative = ground; decorative.sector = 20;
	for(auto& v : decorative.corners) v[2] = 2;
	wall.corners = { vec3{ 0, -5, -5 }, vec3{ 0, 5, -5 }, vec3{ 0, 0, 5 } };
	wall.normal = { 1, 0, 0 }; wall.sector = 20;
	water = decorative; water.flags = surface::water;
	for(auto& v : water.corners) v[2] = 1;
	game::world_collision world({ ground, decorative, wall, water });
	const std::uint32_t own[] = { 10 }, neighbours[] = { 10, 20 };
	auto scoped = world.in_sectors(own);
	REQUIRE(world.floor_at(1, 0, 3, -1));
	CHECK(world.floor_at(1, 0, 3, -1)->height == 2);
	REQUIRE(scoped.floor_at(1, 0, 3, -1));
	CHECK(scoped.floor_at(1, 0, 3, -1)->height == 0);
	CHECK_FALSE(scoped.water_at(1, 0, 3, -1));
	CHECK_FALSE(scoped.first_hit({ 1, 0, 1 }, { -1, 0, 1 }));
	CHECK(scoped.wall_push(0.1f, 0, 0.5f, 1.5f, 0.5f) == std::array<float, 2>{0, 0});
	CHECK_FALSE(scoped.sphere_touch({ 0.1f, 0, 1 }, 0.5f, 0));
	auto joined = world.in_sectors(neighbours);
	CHECK(joined.floor_at(1, 0, 3, -1)->height == 2);
	CHECK(joined.water_at(1, 0, 3, -1).has_value());
	CHECK(joined.first_hit({ 1, 0, 1 }, { -1, 0, 1 }).has_value());
	CHECK(joined.wall_push(0.1f, 0, 0.5f, 1.5f, 0.5f)[0] > 0);
	CHECK(joined.sphere_touch({ 0.1f, 0, 1 }, 0.5f, 0).has_value());
	// Selecting a view must not change the original or an earlier view.
	CHECK(scoped.floor_at(1, 0, 3, -1)->height == 0);
	CHECK(world.floor_at(1, 0, 3, -1)->height == 2);
}

TEST_CASE("screen objects unproject the viewport centre in camera space")
{
	affine camera;
	camera.m[0][3] = 3;
	camera.m[1][3] = 4;
	camera.m[2][3] = 5;
	auto centre = game::screen_object_position(camera, { 500, 500, 1000 });
	CHECK(centre[0] == doctest::Approx(3));
	CHECK(centre[1] == doctest::Approx(3.7)); // 0.2 from 0x422940, times our 1.5 screen-pass distance
	CHECK(centre[2] == doctest::Approx(5));
}

TEST_CASE("camera collision preserves small movements near a wall")
{
	collision_triangle wall;
	wall.corners = { vec3{ 0, -10, -10 }, vec3{ 0, 10, -10 }, vec3{ 0, 0, 10 } };
	wall.normal = { 1, 0, 0 };
	game::world_collision world({ wall });

	// Acceleration after stopping can start below the collision margin, even in free space.
	auto free = world.slide_sphere({ 1, 0, 0 }, { 1, 0.01f, 0 }, 0.35f);
	CHECK(free[1] == doctest::Approx(0.01f));

	// A short movement into the wall must still carry its sideways component along it.
	auto slide = world.slide_sphere({ 0.36f, 0, 0 }, { 0.34f, 0.01f, 0 }, 0.35f);
	CHECK(slide[0] >= 0.35f);
	CHECK(slide[1] == doctest::Approx(0.01f));
}

TEST_CASE("camera offset rays preserve the executable endpoint scaling")
{
	collision_triangle lower, upper;
	lower.corners = { vec3{ 0, -2, -4 }, vec3{ 0, 2, -4 }, vec3{ 0, 2, 4 } };
	upper.corners = { vec3{ 0, -2, -4 }, vec3{ 0, 2, 4 }, vec3{ 0, -2, 4 } };
	lower.normal = upper.normal = { 1, 0, 0 };
	game::world_collision world({ lower, upper });
	CHECK(game::camera_route_obstructed(world, { 2, 0, 0 }, { -2, 0, 0 }, 0.35f, 0.35f, 1));
	CHECK_FALSE(game::camera_route_obstructed(world, { 2, 0, 0 }, { -2, 0, 0 }, 0.35f, 0.35f, 0));
	// Centre clears the wall; the first of the three ring rays clips its edge.
	CHECK_FALSE(game::camera_route_obstructed(world, { 2, 2.05f, 0 }, { -2, 2.05f, 0 }, 0.35f, 0.35f, 1));
	CHECK(game::camera_route_obstructed(world, { 2, 2.05f, 0 }, { -2, 2.05f, 0 }, 0.35f, 0.35f, 3));
	// 0.35 squared is 0.1225, so this ring clears, unlike a radius-0.35 sphere.
	CHECK_FALSE(game::camera_route_obstructed(world, { 2, 2.2f, 0 }, { -2, 2.2f, 0 }, 0.35f, 0.35f, 3));
	// A vertical route uses X as the perpendicular reference instead of Z.
	CHECK_FALSE(game::camera_route_obstructed(world, { 1, 0, -1 }, { 1, 0, 1 }, 0.35f, 0.35f, 3));
}
