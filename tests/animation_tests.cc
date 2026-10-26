// First draft of these tests written by the local model, then reviewed.

#include <openrayman/animation.h>

#include <doctest/doctest.h>

#include <cmath>

using namespace openrayman;

static void check_identity(const std::array<float, 9>& m)
{
	for(int i = 0; i < 9; i++)
		CHECK(m[i] == doctest::Approx(i % 4 == 0 ? 1.0f : 0.0f));
}

TEST_CASE("quaternion_to_matrix: identity, also unnormalized and zero")
{
	check_identity(quaternion_to_matrix(0, 0, 0, 1));
	check_identity(quaternion_to_matrix(0, 0, 0, 3));
	check_identity(quaternion_to_matrix(0, 0, 0, 0));
}

TEST_CASE("quaternion_to_matrix: 90 degrees about Z turns X into Y")
{
	const float half = 3.14159265f / 4;
	const auto m = quaternion_to_matrix(0, 0, std::sin(half), std::cos(half));
	CHECK(m[0] == doctest::Approx(0.0f).epsilon(1e-6));
	CHECK(m[3] == doctest::Approx(1.0f));
	CHECK(m[6] == doctest::Approx(0.0f).epsilon(1e-6));
}
