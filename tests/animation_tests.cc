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

// One animation of four frames with one channel and keyframes at frames 0 and 2: it moves from
// the origin to x = 4 and turns 90 degrees about Z, then holds still after its last keyframe.
static animation_bank two_keyframe_bank()
{
	animation_bank bank;
	animation a;
	a.frames = 4;
	a.channels = 1;
	bank.animations.push_back(a);
	bank.frames.resize(4);
	bank.channels.push_back({});
	bank.ntto_choices.push_back(0);
	bank.objects.push_back({ 0, 5 });
	bank.frame_keys = { 0, 0, 1, 1 };
	bank.vectors = { { 1, 1, 1 }, { 0, 0, 0 }, { 4, 0, 0 } };
	const float half = 3.14159265f / 4;
	// The second rotation is stored as -q, which is the same rotation: blending must take the short way.
	bank.quaternions = { { 0, 0, 0, 1 }, { 0, 0, -std::sin(half), -std::cos(half) } };
	animation_keyframe first, last;
	first.position_vector = 1;
	last.frame = 2;
	last.flags = 0x80;
	last.quaternion = 1;
	last.position_vector = 2;
	bank.keyframes = { first, last };
	return bank;
}

TEST_CASE("pose: blends between keyframes and holds after the last one")
{
	const animation_bank bank = two_keyframe_bank();
	auto x_at = [&](double frame) {
		auto channels = pose(bank, 0, frame);
		REQUIRE(channels.size() == 1);
		CHECK(channels[0].object == std::optional<std::uint16_t>(std::uint16_t(5)));
		return channels[0].local.m[0][3];
	};
	CHECK(x_at(0) == doctest::Approx(0.0f));
	CHECK(x_at(0.5) == doctest::Approx(1.0f));
	CHECK(x_at(1) == doctest::Approx(2.0f));
	CHECK(x_at(2) == doctest::Approx(4.0f));
	CHECK(x_at(3.5) == doctest::Approx(4.0f)); // after the last keyframe
	CHECK(x_at(4.5) == doctest::Approx(1.0f)); // wraps around
	CHECK(x_at(-0.5) == doctest::Approx(4.0f));

	// Halfway through the turn: 45 degrees, not the long way round.
	auto halfway = pose(bank, 0, 1);
	CHECK(std::abs(halfway[0].local.m[0][0]) == doctest::Approx(std::sqrt(0.5f)));
	CHECK(std::abs(halfway[0].local.m[1][0]) == doctest::Approx(std::sqrt(0.5f)));
}
