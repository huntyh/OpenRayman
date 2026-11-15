#include <openrayman/game/mechanics.h>
#include <openrayman/game/camera.h>
#include <openrayman/game/animation_player.h>

#include <doctest/doctest.h>

#include <cmath>

using namespace openrayman;

TEST_CASE("camera mechanics eases short slope corrections inside the speed cap")
{
	game::live_memory memory(memory_image{});
	memory.allocate(16); // reserve the null address in this otherwise empty image
	auto so = memory.allocate(0x80), transform = memory.allocate(0x58);
	memory.write(so + 0x20, transform);
	memory.write(so + 0x24, transform);
	affine pose;
	pose.m[1][3] = 10;
	game::set_global_placement(memory, so, pose);
	game::camera_state state;
	state.valid = true;
	state.flags = 4 | 0x40;
	state.aim = state.off = {0, 0, 0};
	state.move = {0, 0, 0.1f};
	state.speed = 12; // 0.2/tick: the entire correction fits under the speed cap
	state.speed_ramp = 5;
	state.arrive = 2;
	game::move_camera(memory, so, state, 1000.0/60, {});
	CHECK(game::global_placement(memory, so)->m[2][3] == doctest::Approx(0.04));
	// With carried speed, arrival rather than acceleration limits the next step.
	game::set_global_placement(memory, so, pose);
	state.previous_move = {0, 0, 0.1f};
	game::move_camera(memory, so, state, 1000.0/60, {});
	CHECK(game::global_placement(memory, so)->m[2][3] == doctest::Approx(0.05));
	// +0x70 bit 1 bypasses both eases, independently of the snap bit.
	game::set_global_placement(memory, so, pose);
	state.flags |= 2;
	game::move_camera(memory, so, state, 1000.0/60, {});
	CHECK(game::global_placement(memory, so)->m[2][3] == doctest::Approx(0.1));
	// A zero allowed orbit keeps the old direction, while retaining the new radius.
	game::set_global_placement(memory, so, pose);
	state.flags = 0x40;
	state.arrive = state.speed_ramp = 0;
	state.look_at = {0, 0, 0};
	state.turn_speed = 0;
	game::move_camera(memory, so, state, 1000.0/60, {});
	CHECK(game::global_placement(memory, so)->m[2][3] == doctest::Approx(0));
}

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

TEST_CASE("screen placement preserves logo handedness and a face in front of its glow")
{
	// The fixed screen camera's projection (0x4052C0 / 0x422AB0) makes own +X screen-right,
	// own +Z upright, and own -Y towards the viewer. Use a turned game camera to check that
	// these relationships hold independently of the world view, with a nonuniform pulse scale.
	affine camera;
	camera.m = {{{0, -1, 0, 3}, {1, 0, 0, 4}, {0, 0, 1, 5}}};
	affine own;
	own.m = {{{2, 0, 0, 0}, {0, 3, 0, 0}, {0, 0, 4, 0}}};
	const auto saved = own;
	const auto placed = game::screen_object_placement(camera, {500, 200, 10}, own);
	const auto centre = placed.apply({0, 0, 0});
	const auto right = placed.apply({1, 0, 0});
	const auto up = placed.apply({0, 0, 1});
	const auto face = placed.apply({0, -0.1f, 0});
	float face_depth = 0;
	for(int axis = 0; axis < 3; ++axis)
	{
		CHECK(right[axis] - centre[axis] == doctest::Approx(-2 * camera.m[axis][0]));
		CHECK(up[axis] - centre[axis] == doctest::Approx(4 * camera.m[axis][2]));
		face_depth += (face[axis] - centre[axis]) * -camera.m[axis][1];
	}
	CHECK(face_depth < 0); // an additive sprite at the origin is behind the opaque face
	CHECK(own == saved); // placement must not feed a drawn turn/scale back into the scripted rotation
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
