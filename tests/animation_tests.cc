// First draft of these tests written by the local model, then reviewed.

#include <openrayman/animation.h>
#include <openrayman/actor.h>

#include <doctest/doctest.h>

#include <cmath>

using namespace openrayman;

static animation_bank two_keyframe_bank();

static void check_identity(const std::array<float, 9>& m)
{
	for(int i = 0; i < 9; i++)
		CHECK(m[i] == doctest::Approx(i % 4 == 0 ? 1.0f : 0.0f));
}

TEST_CASE("pose: interpolated position follows the rotating channel pivot")
{
	animation_bank bank = two_keyframe_bank();
	bank.vectors.push_back({ 1, 0, 0 });
	bank.channels[0].vector = 3;
	bank.keyframes[0].pivot_position = { 1, 0, 0 };
	bank.keyframes[1].pivot_position = { 1, 0, 0 };
	const auto part = pose(bank, 0, 1)[0].local;
	// The pivot stays at (1, 0), so the part's origin moves as it turns.
	CHECK(part.m[0][3] + part.m[0][0] == doctest::Approx(1.0f));
	CHECK(part.m[1][3] + part.m[1][0] == doctest::Approx(0.0f).epsilon(1e-6));
	bank.keyframes[0].flags |= 0x8;
	const auto held = pose(bank, 0, 1)[0].local;
	CHECK(held.m[0][3] == doctest::Approx(0.0f));
	CHECK(held.m[1][3] == doctest::Approx(0.0f));
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
	bank.channels[0].vector = 1; // zero pivot
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
	last.pivot_position = { 4, 0, 0 };
	bank.keyframes = { first, last };
	return bank;
}

TEST_CASE("pose: morphs match channel and whole frame")
{
	auto bank = two_keyframe_bank();
	bank.channels[0].id = 12;
	bank.animations[0].morphs = 2;
	bank.morphs = { { 16, 42, 12, 1, 0 }, { 9, 100, 12, 2, 0 } };
	CHECK_FALSE(pose(bank, 0, 0.9)[0].morph);
	auto blended = pose(bank, 0, 1.9)[0];
	REQUIRE(blended.morph);
	CHECK(blended.morph->target == 16);
	CHECK(blended.morph->amount == 42);
	CHECK(pose(bank, 0, 2)[0].morph->target == 9);
	CHECK_FALSE(pose(bank, 0, 3)[0].morph);
	actor_set::entry actor;
	actor.animation = 0;
	actor.meshes.resize(17);
	actor.scales.resize(17);
	actor.meshes[5] = 3;
	actor.meshes[9] = 4;
	actor.meshes[16] = 7;
	std::vector<posed_part> parts;
	pose_actor(actor, bank, 1, parts);
	REQUIRE(parts.size() == 1);
	CHECK(parts[0].mesh == 3);
	CHECK(parts[0].morph_mesh == 7);
	CHECK(parts[0].morph_amount == 42);
	parts.clear();
	pose_actor(actor, bank, 2, parts);
	REQUIRE(parts.size() == 1);
	CHECK(parts[0].mesh == 4);
	CHECK_FALSE(parts[0].morph_mesh);
}

TEST_CASE("pose: parent changes hold local keys, including loop blends")
{
	auto bank = two_keyframe_bank();
	bank.animations[0].channels = 3;
	bank.channels[0].id = 12;
	// Parent ids deliberately differ from their channel indices.
	for(std::int16_t id : { 21, 37 })
	{
		auto parent = bank.channels[0];
		parent.id = id;
		parent.frame_keys = static_cast<std::uint32_t>(bank.frame_keys.size());
		bank.frame_keys.insert(bank.frame_keys.end(), 4, 2);
		bank.channels.push_back(parent);
	}
	bank.keyframes.push_back(bank.keyframes[0]);
	bank.keyframes[2].flags = 0x80;
	bank.hierarchies = { { 12, 21 }, { 12, 37 } };
	for(std::size_t f = 0; f < bank.frames.size(); f++)
	{
		bank.frames[f].hierarchy_count = 1;
		bank.frames[f].hierarchy_start = f < 2 ? 0 : 1;
	}
	bank.vectors.push_back({ 2, 3, 4 });
	bank.keyframes[1].scale_vector = 3;
	bank.keyframes[0].ease = 1;
	bank.keyframes[1].ease = 8192;
	const auto first = pose(bank, 0, 0)[0];
	const auto last = pose(bank, 0, 2)[0];
	auto check_local = [](const affine& actual, const affine& expected) {
		for(int r = 0; r < 3; r++)
			for(int c = 0; c < 4; c++)
				CHECK(actual.m[r][c] == doctest::Approx(expected.m[r][c]));
	};

	SUBCASE("head to hand: position, rotation and scale switch with the parent")
	{
		for(double frame : { 0.5, 1.0, 1.5, 1.99 })
		{
			const auto held = pose(bank, 0, frame)[0];
			CHECK(held.parent == std::optional<std::size_t>(1));
			check_local(held.local, first.local);
		}
		CHECK(last.parent == std::optional<std::size_t>(2));
		CHECK(last.local.m[0][3] == doctest::Approx(4.0f));
		CHECK(last.local.uniform_scale() > first.local.uniform_scale());
		CHECK(std::abs(last.local.m[1][0]) > 1.0f);
	}
	SUBCASE("root to parent also holds")
	{
		bank.frames[0].hierarchy_count = bank.frames[1].hierarchy_count = 0;
		const auto held = pose(bank, 0, 1.5)[0];
		CHECK_FALSE(held.parent);
		check_local(held.local, first.local);
	}
	SUBCASE("different hierarchy records with the same parent still interpolate and ease")
	{
		bank.hierarchies[1][1] = 21;
		CHECK(pose(bank, 0, 1)[0].local.m[0][3] == doctest::Approx(1.0f));
	}
	SUBCASE("0x200 cannot blend back into another parent's space")
	{
		bank.keyframes[1].flags |= 0x200;
		check_local(pose(bank, 0, 3.5)[0].local, last.local);
	}
	SUBCASE("seam glide cannot blend back into another parent's space")
	{
		check_local(pose(bank, 0, 3.5, 2)[0].local, last.local);
		bank.keyframes[1].flags |= 0x200;
		check_local(pose(bank, 0, 3.5, 2)[0].local, last.local);
		CHECK(pose(bank, 0, 4, 2)[0].parent == first.parent);
		check_local(pose(bank, 0, 4, 2)[0].local, first.local);
	}
}

TEST_CASE("pose: blends between keyframes, holds after the last, loops back when flagged, eases")
{
	animation_bank bank = two_keyframe_bank();
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
	CHECK(x_at(3.5) == doctest::Approx(4.0f)); // after the last keyframe: held
	CHECK(x_at(4.5) == doctest::Approx(1.0f)); // wraps around
	CHECK(x_at(-0.5) == doctest::Approx(4.0f));

	// Flag 0x200 on the last keyframe: frames 2..4 blend back to the first.
	bank.keyframes[1].flags = 0x280;
	CHECK(x_at(3) == doctest::Approx(2.0f));
	CHECK(x_at(3.5) == doctest::Approx(1.0f));

	// Easing towards the second keyframe: t = 0.5 becomes 0.5 * (1 + 1 * -0.5) = 0.25.
	bank.keyframes[0].ease = 1; // the current key enables the next key's easing
	bank.keyframes[1].ease = 8192;
	CHECK(x_at(1) == doctest::Approx(1.0f));
	bank.keyframes[1].flags = 0x80;
	bank.keyframes[1].ease = 0;
	bank.keyframes[0].ease = 0;

	// Our loop seam glide: over the last quarter (frame 3..4) it slides from the held x = 4 into the
	// first frame's x = 0; halfway (smoothstep 0.5) is x = 2.
	CHECK(pose(bank, 0, 3.5, 2)[0].local.m[0][3] == doctest::Approx(2.0f));
	CHECK(pose(bank, 0, 2.5, 2)[0].local.m[0][3] == doctest::Approx(4.0f));
	// The glide blends final positions: a held x of 4 × multiplier 2 = 8 is halfway at 4, not at
	// (4 → 0) × (2 → 1) = 3.
	bank.keyframes[1].position_multiplier = 2;
	CHECK(pose(bank, 0, 2.5, 2)[0].local.m[0][3] == doctest::Approx(8.0f));
	CHECK(pose(bank, 0, 3.5, 2)[0].local.m[0][3] == doctest::Approx(4.0f));
	bank.keyframes[1].position_multiplier = 1;

	// Halfway through the turn: 45 degrees, not the long way round.
	auto halfway = pose(bank, 0, 1);
	CHECK(std::abs(halfway[0].local.m[0][0]) == doctest::Approx(std::sqrt(0.5f)));
	CHECK(std::abs(halfway[0].local.m[1][0]) == doctest::Approx(std::sqrt(0.5f)));
}
