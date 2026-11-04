#include <openrayman/game/mechanics.h>
#include <openrayman/game/camera.h>

#include <doctest/doctest.h>

#include <cmath>

using namespace openrayman;

TEST_CASE("screen objects unproject the viewport centre in camera space")
{
	affine camera;
	camera.m[0][3] = 3;
	camera.m[1][3] = 4;
	camera.m[2][3] = 5;
	auto centre = game::screen_object_position(camera, { 500, 500, 1000 });
	CHECK(centre[0] == doctest::Approx(3));
	CHECK(centre[1] == doctest::Approx(3.8));
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

TEST_CASE("camera finds a reachable view around a finite wall")
{
	collision_triangle lower, upper;
	lower.corners = { vec3{ 0, -2, -4 }, vec3{ 0, 2, -4 }, vec3{ 0, 2, 4 } };
	upper.corners = { vec3{ 0, -2, -4 }, vec3{ 0, 2, 4 }, vec3{ 0, -2, 4 } };
	lower.normal = upper.normal = { 1, 0, 0 };
	game::world_collision world({ lower, upper });
	vec3 target{ 2, 0, 0 }, camera{ -2, 0, 0 };
	REQUIRE(world.first_hit(target, camera));
	auto waypoint = game::camera_obstacle_waypoint(world, target, camera, target, 0.35f);
	REQUIRE(waypoint);
	double waypoint_distance = std::sqrt(std::pow((*waypoint)[0] - target[0], 2) + std::pow((*waypoint)[1] - target[1], 2) + std::pow((*waypoint)[2] - target[2], 2));
	CHECK(waypoint_distance == doctest::Approx(4.0f));
	CHECK_FALSE(world.first_hit(target, *waypoint));
	auto reached = world.slide_sphere(camera, *waypoint, 0.35f);
	for(int i = 0; i < 3; i++)
		CHECK(reached[std::size_t(i)] == doctest::Approx((*waypoint)[std::size_t(i)]).epsilon(0.001));
	CHECK_FALSE(game::camera_obstacle_waypoint(world, target, vec3{ 1, 0, 0 }, vec3{ 1, 0, 0 }, 0.35f));

	// The view is clear, but the camera is already within the wider clearance of the wall.
	vec3 near_target{ -3, 0, 0 }, near_camera{ -0.4f, 0, 0 };
	CHECK_FALSE(world.first_hit(near_target, near_camera));
	auto early = game::camera_obstacle_waypoint(world, near_target, near_camera, near_camera, 0.35f);
	REQUIRE(early);
	CHECK((*early)[0] <= -0.4375f);
	CHECK_FALSE(game::camera_obstacle_waypoint(world, near_target, vec3{ -0.5f, 0, 0 }, vec3{ -0.5f, 0, 0 }, 0.35f));

	// Even with a clear view and no nearby wall, the proposed follow path crosses the wall.
	vec3 path_target{ -3, 3, 0 }, path_camera{ -2, -3, 0 }, desired{ 2, 0, 0 };
	CHECK_FALSE(world.first_hit(path_target, path_camera));
	auto route = game::camera_obstacle_waypoint(world, path_target, path_camera, desired, 0.35f);
	REQUIRE(route);
	CHECK_FALSE(world.first_hit(path_target, *route));
	auto route_end = world.slide_sphere(path_camera, *route, 0.35f);
	for(int i = 0; i < 3; i++)
		CHECK(route_end[std::size_t(i)] == doctest::Approx((*route)[std::size_t(i)]).epsilon(0.001));
}
