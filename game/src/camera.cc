#include <openrayman/game/camera.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/game/interpreter.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <cstring>

// The game camera: the three stages of the solve its scripts run every frame (procedure 444 ->
// 0x485810) and the mechanics tick that moves the camera super object towards the result (0x4359D0).
// Names are ours; the addresses are the exe's. See docs/formats/camera.md.

namespace openrayman::game
{
	std::array<float, 3> screen_object_position(const affine& camera, const std::array<int, 3>& screen)
	{
		// 0x422940: depth is reciprocal projection depth, in thousandths; 0 is the
		// special near-plane case. The renderer uses a 0.9 rad vertical FOV and 0.1 near plane.
		float z = float(screen[2]) * 0.001f;
		float distance = z == 0 ? 0.0f : 0.1f * (z + 1.0f) / z;
		// 0x422940 divides the lateral coordinates by depth independently of
		// the forward coordinate; at zero it skips that division.
		float half_height = (z == 0 ? 0.1f : 0.1f / z) * std::tan(0.45f);
		float x = (float(screen[0]) * 0.002f - 1.0f) * half_height * (4.0f / 3.0f);
		float y = (1.0f - float(screen[1]) * 0.002f) * half_height;
		// Ours: the exe draws these in a separate screen pass whose projection isn't traced yet;
		// side by side with the game (user 2026-11-04, the menu butterfly at New Game) ours came
		// out about 1.5 times too big, so the point sits 1.5 times further away along the same
		// ray (same place on screen, two thirds the size).
		constexpr float screen_pass_distance = 1.5f;
		distance *= screen_pass_distance;
		x *= screen_pass_distance;
		y *= screen_pass_distance;
		std::array<float, 3> result{};
		for(int i = 0; i < 3; ++i)
			result[i] = camera.m[i][3] - camera.m[i][0] * x - camera.m[i][1] * distance + camera.m[i][2] * y;
		return result;
	}

	affine screen_object_placement(const affine& camera, const std::array<int, 3>& screen, const affine& own)
	{
		// The exe draws these objects in a screen pass of their own, with a fixed camera (0x4052C0:
		// at (0, 0, 5), turned a quarter about X by 0x445BE0 and a half about Z by 0x445C70), so an
		// object keeps the orientation it has in front of that camera whatever the game camera does
		// (identity for the intro's Ubisoft logo; 409 sets only its scale, which we leave). Ours draws it in the world
		// in front of the game camera, so it turns with that camera. The fixed view maps the object's
		// X, Y, Z to -view X, view depth, view Y. Including the negative projection factors at
		// 0x422AB0 gives screen right = own +X and up = own +Z. In our camera-super-object axes
		// (right=-X, forward=-Y, up=Z), this is the half turn below. This agrees with the earlier
		// logo comparison: changing it would mirror the lettering. Preserve the object's own scale
		// and rotation; the screen draw's depth/order rules apply separately (0x45CC80).
		affine turn;
		for(int r = 0; r < 3; r++)
		{
			turn.m[r][0] = -camera.m[r][0];
			turn.m[r][1] = -camera.m[r][1];
			turn.m[r][2] = camera.m[r][2];
			turn.m[r][3] = 0;
		}
		affine rotation = own;
		for(int r = 0; r < 3; r++)
			rotation.m[r][3] = 0;
		affine a = turn * rotation;
		auto point = screen_object_position(camera, screen);
		for(int i = 0; i < 3; ++i)
			a.m[i][3] = point[i];
		return a;
	}

	namespace
	{
		namespace camera_field
		{
			constexpr std::uint32_t block_default = 0x00, block_current = 0x04, block_spare = 0x08, block_override = 0x0C;
			constexpr std::uint32_t block_size = 0x7C;
			constexpr std::uint32_t override_active = 0x74; // u8, bit 0: the override block holds the live copy
			// The parameter block: two vectors, then the numbers procedures 447..466 write.
			constexpr std::uint32_t vector_a = 0x00, vector_b = 0x0C;
			constexpr std::uint32_t min_distance = 0x18, max_distance = 0x1C;
			constexpr std::uint32_t speed = 0x38, speed_ramp = 0x3C, arrive = 0x40;
			constexpr std::uint32_t turn_speed = 0x44, turn_ramp = 0x48, turn_arrive = 0x4C;
			constexpr std::uint32_t aim_speed = 0x50, aim_ramp = 0x54, aim_arrive = 0x58;
			constexpr std::uint32_t min_height = 0x60, max_height = 0x64;
			constexpr std::uint32_t target = 0x68;
			constexpr std::uint32_t flags_70 = 0x70, flags_72 = 0x72;
		}
		namespace camera_object_field
		{
			constexpr std::uint32_t vectors = 0x28; // 495's vector A, 496's fixed position, 497's vector pair
		}
		// Flags of the block +0x72 word the solve reads.
		constexpr std::uint32_t fixed_vector_a = 0x1000, fixed_position = 0x2000;
		// Flags of the block +0x70 word the mechanics reads (the exe's latched +0x25E has the same
		// first meanings: 0x01 snaps the position, 0x04 keeps the orientation, 0x10 snaps the aim
		// point, 0x80 takes the aim point from the camera's own axes).
		constexpr std::uint32_t snap_position = 0x01, keep_orientation = 0x04, aim_snap = 0x10, fixed_aim = 0x80;

		float dot(const vec3& a, const vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
		vec3 add(const vec3& a, const vec3& b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
		vec3 sub(const vec3& a, const vec3& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
		vec3 mul(const vec3& a, float f) { return { a[0] * f, a[1] * f, a[2] * f }; }
		vec3 cross(const vec3& a, const vec3& b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
		float length(const vec3& a) { return std::sqrt(dot(a, a)); }
		vec3 normalised(const vec3& a)
		{
			float l = length(a);
			return l > 1e-9f ? mul(a, 1.0f / l) : a;
		}
		vec3 read3(const live_memory& m, std::uint32_t at) { return { m.f32(at), m.f32(at + 4), m.f32(at + 8) }; }

		// A vector given in an actor's own axes, in the world. The transform keeps the three axes as
		// its rows (+0x10, +0x1C, +0x28) and the game multiplies row vectors, so this is the block
		// vectors' rotation 0x444AC0 does.
		vec3 rotate(const live_memory& m, std::uint32_t transform, const vec3& v)
		{
			vec3 out{};
			for(int i = 0; i < 3; i++)
				out = add(out, mul(read3(m, transform + 0x10 + std::uint32_t(i) * 12), v[std::size_t(i)]));
			return out;
		}

		// The super object a parameter block names as its target (+0x68), or 0. Procedure 467 stores
		// the actor's super object there; an actor pointer resolves too (0x4136D0).
		std::uint32_t target_super_object(interpreter& vm, std::uint32_t block)
		{
			live_memory& m = vm.memory();
			std::uint32_t at = m.contains(block + camera_field::target, 4) ? m.u32(block + camera_field::target) : 0;
			if(!at || !m.contains(at, 4))
				return 0;
			if(vm.actor_of(at))
				return at;
			std::uint32_t super_object = vm.super_object_of(at);
			return super_object && vm.actor_of(super_object) == at ? super_object : 0;
		}

		// A super object's global transform (+0x24), or 0 when it has none.
		std::uint32_t global_transform(const live_memory& m, std::uint32_t super_object)
		{
			if(!super_object || !m.contains(super_object + 0x24, 4))
				return 0;
			std::uint32_t at = m.u32(super_object + 0x24);
			return at && m.contains(at, 0x58) ? at : 0;
		}

		// A point in front of a transform's position: the camera's -Y direction, as 0x4341BF builds
		// one (the position minus its second axis).
		vec3 front_point(const live_memory& m, std::uint32_t transform)
		{
			vec3 y = normalised(read3(m, transform + 0x1C));
			if(length(y) < 1e-6f)
				y = { 0, 1, 0 };
			return sub(read3(m, transform + 4), y);
		}

		// 0x440990: a vector turned about an axis (Rodrigues).
		vec3 rotate_about(const vec3& v, const vec3& axis, float angle)
		{
			float c = std::cos(angle), s = std::sin(angle);
			vec3 a = cross(axis, v), d = mul(axis, dot(axis, v));
			return add(add(mul(v, c), mul(a, s)), mul(d, 1.0f - c));
		}

		// 0x433930: the movement's direction is turned a part of the way towards the previous tick's
		// direction and keeps the movement's length. The angle between the two unit vectors is cut by
		// the divisor (2 leaves the movement halfway between them, less than 1 overshoots). A divisor
		// of 1, or either vector shorter than 0.1, leaves the movement alone (the exe's constants
		// 0x49C6FC = 1 and 0x49C708 = 0.1).
		vec3 smoothed_movement(const vec3& move, const vec3& previous, float divisor)
		{
			if(divisor == 1.0f)
				return move;
			float move_length = length(move), previous_length = length(previous);
			if(!(move_length > 0.1f && previous_length > 0.1f))
				return move;
			vec3 from = mul(previous, 1.0f / previous_length), to = mul(move, 1.0f / move_length);
			vec3 axis = cross(from, to);
			float sine = length(axis);
			// 0x4407A0 sorts the angle out of the cross product; parallel vectors have no axis, and
			// the exe's rotation about a zero axis is degenerate, so leave the movement as it is.
			if(sine < 1e-6f)
				return move;
			float angle = std::acos(std::max(-1.0f, std::min(1.0f, dot(from, to))));
			axis = mul(axis, 1.0f / sine);
			return mul(normalised(rotate_about(from, axis, angle / divisor)), move_length);
		}
	}

	namespace
	{
		constexpr float pi = 3.14159265f, two_pi = 6.28318531f;

		float wrap_two_pi(float a)
		{
			a = std::fmod(a, two_pi);
			return a < 0 ? a + two_pi : a;
		}

		float wrap_pi(float a)
		{
			a = wrap_two_pi(a);
			return a > pi ? a - two_pi : a;
		}

		// 0x4841F0: the angle to use, given the measured one and a window (centre, half width, in
		// radians). Half >= pi leaves it free, half <= 0 holds it at the centre; in between it stays
		// where it is inside the window and goes to the nearer edge outside. The exe also keeps the
		// measured angle moved at most pi/8 towards it (context +0x78), which solve_camera uses to
		// swing the camera round in steps instead of cutting across (0x4855D0).
		float window_angle(float a, float centre, float half)
		{
			if(half >= pi)
				return a;
			if(half <= 0)
				return centre;
			float lo = wrap_two_pi(centre - (half + 1e-4f)), hi = wrap_two_pi(centre + (half + 1e-4f));
			bool inside = hi > lo ? (a >= lo && a <= hi) : (a <= hi || a >= lo);
			if(inside)
				return a;
			return wrap_pi(a - centre) <= 0 ? lo : hi;
		}

		// The way back from the aim point to the camera (0x4847D0 section C): measure the current
		// direction `d` against a reference axis as a turn about the up axis and an elevation
		// (0x484390, 0x433390, 0x4335C0, folded by 0x473A40), pick the angles the block allows
		// (yaw window +0x28/+0x2C, pitch window +0x30/+0x34; 0x4841F0) and rebuild (0x484470).
		struct direction
		{
			vec3 back{};   // the way back from the aim point, unit
			float yaw = 0; // the measured yaw, 0..2pi
			float gap = 0; // from the measured yaw to the one the window asks for, -pi..pi (+0x7C)
		};
		direction camera_direction(const live_memory& m, std::uint32_t cam, std::uint32_t block, std::uint32_t target_transform, vec3 d,
			std::uint16_t flags_72, std::optional<float> yaw_centre = {}, std::optional<float> pitch_centre = {})
		{
			vec3 up{ 0, 0, 1 }, ref{};
			if(flags_72 & 0x4000)
			{
				// 497's fixed axes (0x483BE0 with the 0x4E92FD flag, bit 14 of +0x72; 0x485975).
				ref = normalised(read3(m, cam + 0x40));
				if(length(ref) < 0.5f)
					ref = { 0, -1, 0 };
				up = normalised(read3(m, cam + 0x4C));
				if(length(up) < 0.5f)
					up = { 0, 0, 1 };
			}
			else
			{
				// 0x473BB0: behind the target. Actors look along -Y, so that is their +Y axis
				// (taking -Y put the camera in front of Rayman; user).
				ref = normalised(read3(m, target_transform + 0x1C));
				if(length(ref) < 0.5f)
					ref = { 0, -1, 0 };
			}
			if(d[0] == up[0] && d[1] == up[1])
				d[0] += 0.5f;
			// 0x433390: the signed turn from ref to d about up, both flattened onto the plane.
			auto flat = [&](const vec3& v) { return sub(v, mul(up, dot(v, up))); };
			vec3 fr = flat(ref), fd = flat(d);
			float yaw = 0;
			if(length(fr) > 1e-6f && length(fd) > 1e-6f)
				yaw = std::atan2(dot(up, cross(fr, fd)), dot(fr, fd));
			// 0x4335C0: the difference of their elevations, folded to +-pi/2 (0x473A40).
			auto elevation = [&](const vec3& v) {
				float l = length(v);
				return l > 1e-6f ? std::asin(std::clamp(dot(v, up) / l, -1.0f, 1.0f)) : 0.0f;
			};
			float pitch = elevation(d) - elevation(ref);
			if(std::fabs(pitch) > pi / 2)
				pitch = pitch > 0 ? pi - pitch : -pi - pitch;
			yaw = wrap_two_pi(yaw);
			pitch = wrap_two_pi(pitch);
			// Bit 0x40 of +0x72 holds both at their centres.
			bool hold = flags_72 & 0x40;
			direction out;
			out.yaw = yaw;
			yaw = window_angle(yaw, yaw_centre ? *yaw_centre : m.f32(block + 0x28), hold ? 0 : m.f32(block + 0x2C));
			out.gap = wrap_pi(yaw - out.yaw);
			pitch = window_angle(pitch, pitch_centre.value_or(m.f32(block + 0x30)), hold ? 0 : m.f32(block + 0x34));
			// 0x484470: turn ref about up, then raise it about the axis across.
			vec3 v1 = rotate_about(ref, up, yaw);
			vec3 across = normalised(cross(v1, up));
			out.back = length(across) > 0.5f ? normalised(rotate_about(v1, across, pitch)) : normalised(v1);
			return out;
		}

		// 0x4845E0: project the reference direction onto the target's floor,
		// then weight its tilt by the direction from the wanted eye to the aim.
		// Keep this pitch local: the exe changes a scratch block when finding
		// the ideal point/candidates, so each candidate starts with the same pitch.
		float floor_pitch(interpreter& vm, std::uint32_t cam, std::uint32_t block,
			const vec3& ref, const vec3& back, float pitch)
		{
			auto& m = vm.memory();
			if((m.read<std::uint16_t>(block + 0x72) & 0x20) || (m.u8(cam + 0x74) & 2)) return pitch;
			auto word = [&](std::uint32_t at) { return at && m.contains(at, 4) ? m.u32(at) : 0u; };
			auto actor = vm.actor_of(target_super_object(vm, block));
			// 0x473660: the valid ground contact (slot 1), not slot 0 or a
			// remembered/smoothed normal. Losing contact also skips fitting on a jump.
			auto base = word(word(actor + 8)), report = word(base + 0x150);
			if(!report || !(word(report + 4) & 1)) return pitch;
			vec3 normal = read3(m, report + 0x34);
			vec3 flat = normalised(sub(ref, mul(normal, dot(ref, normal))));
			if(length(flat) < 0.5f) return pitch;
			float tilt = (std::acos(std::clamp(flat[2], -1.0f, 1.0f)) - pi / 2) * dot(mul(back, -1), flat);
			float centre = m.u8(cam + 0x75) & 2 ? m.f32(m.u32(cam + 8) + 0x30) : pitch;
			if(centre * tilt >= 0) {
				if(std::fabs(tilt) < std::fabs(centre)) return pitch;
				tilt -= centre;
			}
			return centre + tilt;
		}

		direction fitted_direction(interpreter& vm, std::uint32_t cam, std::uint32_t block,
			std::uint32_t target_transform, const vec3& delta, std::uint16_t flags,
			std::optional<float> yaw = {}, bool slope = true)
		{
			auto& m = vm.memory();
			auto dir = camera_direction(m, cam, block, target_transform, delta, flags, yaw);
			if(slope)
			{
				vec3 ref = normalised(read3(m, flags & 0x4000 ? cam + 0x40 : target_transform + 0x1C));
				float pitch = floor_pitch(vm, cam, block, ref, dir.back, m.f32(block + 0x30));
				if(pitch != m.f32(block + 0x30))
					dir = camera_direction(m, cam, block, target_transform, delta, flags, yaw, pitch);
			}
			return dir;
		}
	}

	// The radius of the camera's collision sphere: the first sphere among its actor's kind-1 zones
	// (actor +0x14 -> the list at +8, the one the movement collides with; a zone is vertices +0,
	// element kinds +0x10, elements +0x14, and a sphere element's first entry starts with its
	// radius). The Fix camera's is 0.35 round its origin. 0 when it has none.
	static float camera_radius(const live_memory& m, std::uint32_t super_object)
	{
		auto word = [&](std::uint32_t at) { return m.contains(at, 4) ? m.u32(at) : 0u; };
		std::uint32_t actor = word(super_object + 4);
		std::uint32_t set = actor ? word(actor + 0x14) : 0;
		if(set)
		{
			std::uint32_t head = word(set + 8);
			for(std::uint32_t node = head ? word(head) : 0, i = 0; node && i < 16; node = word(node), i++)
			{
				std::uint32_t zone = word(node + 4);
				std::uint32_t kinds = zone ? word(zone + 0x10) : 0, data = zone ? word(zone + 0x14) : 0;
				if(!kinds || !data || !m.contains(kinds, 2) || m.read<std::int16_t>(kinds) != 7)
					continue;
				std::uint32_t entries = word(word(data));
				if(entries && m.contains(entries, 4))
					return m.f32(entries);
			}
		}
		return 0;
	}

	bool camera_route_obstructed(const world_collision& world, const vec3& from, const vec3& to,
		float start_radius, float end_radius, unsigned count)
	{
		// 0x485FD0 scales the initial perpendicular by start_radius, then multiplies
		// it again at each endpoint. Preserve that unusual squared start offset.
		auto hit = [&](vec3 a, vec3 b) {
			// 0x485CA0 truncates world coordinates to thousandths before casting.
			for(unsigned k = 0; k < 3; ++k) {
				a[k] = std::trunc(a[k] * 1000) * 0.001f;
				b[k] = std::trunc(b[k] * 1000) * 0.001f;
			}
			return world.first_hit(a, b).has_value();
		};
		if(count == 1) return hit(from, to);
		if(count == 0 || length(sub(to, from)) < 1e-9f) return false;
		vec3 axis = normalised(sub(to, from));
		vec3 side = cross(axis, vec3{ 0, 0, 1 });
		if(length(side) < 1e-9f) side = cross(axis, vec3{ 1, 0, 0 });
		side = mul(normalised(side), start_radius);
		for(unsigned i = 0; i < count; ++i) {
			if(hit(add(from, mul(side, start_radius)), add(to, mul(side, end_radius)))) return true;
			side = rotate_about(side, axis, two_pi / float(count));
		}
		return false;
	}

	namespace
	{
		// A solve context for 0x486400..0x487730. Runtime state lives in the camera
		// object, so script save/restore and mode resets see the same state as the solve.
		struct obstacle_search
		{
			interpreter& vm;
			live_memory& m;
			camera_state& state;
			std::uint32_t cam, block, target_transform;
			vec3 anchor, aim, position, desired, offset_point;
			vec3 ref, up;
			unsigned flags, globals;
			bool static_tests = true;
			mutable std::optional<world_collision> objects;

			float setting(unsigned at) const {
				float f; std::memcpy(&f, &vm.camera().values[at], 4); return f;
			}
			unsigned count(unsigned at) const {
				std::uint16_t n; std::memcpy(&n, &vm.camera().values[at], 2); return n;
			}
			int mode() const { return m.read<int>(cam + 0x60); }
			bool near(const vec3& a, const vec3& b) const { return length(sub(a, b)) <= setting(0x68); }
			void copy_block(std::uint32_t to, std::uint32_t from) {
				if(to && from) std::memcpy(m.span(to, 0x7C).data(), m.span(from, 0x7C).data(), 0x7C);
			}
			bool route(const vec3& from, const vec3& to, bool view, bool extended = false) const {
				vec3 end = to;
				// 0x4862E0 extends the anchor ray 0.35 beyond the candidate.
				if(extended) end = add(to, mul(normalised(sub(to, from)), 0.35f));
				auto blocked = [&](const world_collision& world) {
					return camera_route_obstructed(world, from, end,
						setting(view ? 0x3C : 0x34), setting(view ? 0x40 : 0x38), count(view ? 0x76 : 0x74));
				};
				if(static_tests && blocked(vm.world())) return true;
				// 0x485BE0 also tests actor geometry. Use the posed triangles our
				// movement system already supplies; actor spheres/boxes and the exact
				// collision-set group filter are still absent here. Bit 0x10 disables
				// this actor pass, rather than disabling scenery as the interim code did.
				if(flags & 0x10) return false;
				if(!objects) objects.emplace(actor_collision_triangles(vm, state.super_object), true);
				return blocked(*objects);
			}
			// 0x486400: true means rejected. Bit 1 tests the view, bit 2 the
			// movement; bit 4 replaces the view ray by the extended movement ring.
			bool rejected(const vec3& p, unsigned mask) const {
				if(length(sub(p, anchor)) < m.f32(block + 0x20) + setting(0x6C)) return true;
				return ((mask & 1) && route(anchor, p, !(mask & 4), mask & 4)) ||
					((mask & 2) && route(position, p, false));
			}
			std::array<float, 3> coordinates(const vec3& v) const {
				auto flat = [&](const vec3& x) { return sub(x, mul(up, dot(x, up))); };
				vec3 a = flat(ref), b = flat(v);
				float yaw = length(a) > 1e-6f && length(b) > 1e-6f ? std::atan2(dot(up, cross(a, b)), dot(a, b)) : 0;
				auto elevation = [&](const vec3& x) { return length(x) > 1e-6f ? std::asin(std::clamp(dot(x, up) / length(x), -1.0f, 1.0f)) : 0.0f; };
				float pitch = elevation(v) - elevation(ref);
				if(std::fabs(pitch) > pi / 2) pitch = pitch > 0 ? pi - pitch : -pi - pitch;
				return { length(v), wrap_two_pi(yaw), wrap_two_pi(pitch) };
			}
			vec3 direction_at(float yaw, float pitch) const {
				vec3 v = rotate_about(ref, up, yaw);
				vec3 axis = normalised(cross(v, up));
				return length(axis) > 0.5f ? normalised(rotate_about(v, axis, pitch)) : normalised(v);
			}
			float slope_pitch(float yaw, float pitch) const {
				// 0x484B00 -> 0x4845E0 adjusts candidates for the target's floor.
				return floor_pitch(vm, cam, block, ref, direction_at(yaw, pitch), pitch);
			}
			std::uint32_t original_block() const { return m.u8(cam + 0x75) & 2 ? m.u32(cam + 8) : block; }
			vec3 follow_point(std::uint32_t b, bool hold_centres) const {
				auto options = m.read<std::uint16_t>(b + 0x72);
				if(hold_centres) options |= 0x40;
				auto d = fitted_direction(vm, cam, b, target_transform, sub(position, aim), options, {}, hold_centres || !(m.u8(cam + 0x75) & 8));
				float r = std::clamp(length(sub(position, aim)), m.f32(block + 0x18), std::max(m.f32(block + 0x18), m.f32(block + 0x1C)));
				vec3 p = add(aim, mul(d.back, std::max(r, 0.01f)));
				p[2] = std::clamp(p[2], m.f32(block + 0x60), std::max(m.f32(block + 0x60), m.f32(block + 0x64)));
				return p;
			}
			vec3 ideal() const { return follow_point(original_block(), true); }
			// 0x486540: quantized errors in distance, yaw and pitch from the
			// saved follow block, sorted smallest first by 0x486840.
			float score(const vec3& offset) const {
				auto base = coordinates(sub(ideal(), aim)), c = coordinates(offset);
				auto quantize = [](float x) { x *= 100; return float(-int(x * -2) - int(x)); };
				float by = base[1] > pi ? two_pi - base[1] : base[1];
				float cy = c[1] > pi ? two_pi - c[1] : c[1];
				float bp = base[2] > pi ? two_pi - base[2] : base[2];
				float cp = c[2] > pi ? c[2] - two_pi : c[2];
				return quantize(std::fabs(by - cy)) * 0.01f + quantize(std::fabs(bp - cp)) * 0.1f +
					quantize(std::fabs(base[0] - c[0])) * 0.001f;
			}
			struct candidate { vec3 offset; float score; };
			// 0x486880: rotate the ordinary follow block around the target, at
			// its saved pitch, level, +pi/4 and -pi/4. The fallback also samples
			// half distance, and (with mask 8) 0.33 distance at the saved pitch.
			std::vector<candidate> candidates(unsigned mask) const {
				std::vector<candidate> out;
				auto b = original_block();
				float step = setting(mask & 4 ? 0x48 : 0x44);
				if(!(step > 0)) return out;
				unsigned n = std::min(unsigned(two_pi / step), 256u);
				float yaw = m.f32(b + 0x28), pitch = m.f32(b + 0x30);
				float radius = std::clamp(length(sub(position, aim)), m.f32(block + 0x18), std::max(m.f32(block + 0x18), m.f32(block + 0x1C)));
				float ideal_radius = length(sub(ideal(), aim));
				auto append = [&](float p, float r) {
					float angle = wrap_two_pi(yaw);
					vec3 point = add(aim, mul(direction_at(angle, slope_pitch(angle, p)), std::max(r, 0.01f)));
					point[2] = std::clamp(point[2], m.f32(block + 0x60), std::max(m.f32(block + 0x60), m.f32(block + 0x64)));
					vec3 v = sub(point, aim);
					out.push_back({ v, score(v) });
				};
				for(unsigned i = 0; i < n; ++i, yaw += step) {
					append(pitch, radius);
					if((mask & 4) && !(globals & 0x40)) {
						append(pitch, ideal_radius * 0.5f);
						if(mask & 8) append(pitch, ideal_radius * 0.33f);
					}
					append(0, radius); append(pi / 4, radius); append(-pi / 4, radius);
				}
				return out;
			}
			// 0x4875D0: do not abandon an obstruction waypoint for a candidate
			// closer to the transition's starting point while still moving.
			bool may_leave(const vec3& p, float dt) const {
				if(mode() && m.f32(cam + 0x70) < count(0x7C) * 1000 * dt && (m.u8(cam + 0x75) & 8))
					return length(sub(p, read3(m, cam + 0x10))) > length(sub(read3(m, cam + 0x1C), read3(m, cam + 0x10)));
				return true;
			}
			// 0x486710 followed by 0x4864D0; the former uses the instantaneous
			// anchor, the latter adds the offset to the tracked aim point.
			std::optional<std::pair<vec3, unsigned>> search(unsigned mask, float dt) const {
				auto list = candidates(mask);
				state.trace.search_mask = mask;
				state.trace.search_candidates = unsigned(list.size());
				std::stable_sort(list.begin(), list.end(), [](const candidate& a, const candidate& b) { return a.score < b.score; });
				float previous = score(sub(mask & 4 ? position : desired, aim));
				for(unsigned i = 0; i < list.size(); ++i) {
					const auto& c = list[i];
					vec3 p = add(anchor, c.offset);
					if((mask & 8) && (c.score >= previous || previous - c.score < 0.05f || !may_leave(p, dt))) continue;
					if(length(c.offset) < m.f32(block + 0x20) + setting(0x6C)) continue;
					if(!(mask & 4) && near(p, desired)) continue;
					if((!(mask & 4) || (mask & 8)) && near(p, position)) continue;
					p = add(aim, c.offset);
					if(!rejected(p, mask)) {
						state.trace.search_rank = int(i);
						return std::pair{ p, i };
					}
				}
				return {};
			}
			// 0x487440 / 0x4874F0: fit a follow block to a world destination.
			void fit(std::uint32_t b, const vec3& p) {
				auto c = coordinates(sub(p, aim));
				m.write<float>(b + 0x18, c[0]); m.write<float>(b + 0x1C, c[0]);
				m.write<float>(b + 0x28, c[1]); m.write<float>(b + 0x30, c[2]);
			}
			void store3(unsigned at, const vec3& p) {
				for(unsigned k = 0; k < 3; ++k) m.write<float>(cam + at + k * 4, p[k]);
			}
			// 0x487520. Keep the unmodified block in the spare slot until the
			// transition ends; scripts can continue overriding its other fields.
			void begin(const vec3& p, bool obstruction) {
				if(!(m.u8(cam + 0x75) & 2)) {
					copy_block(m.u32(cam + 8), m.u32(cam + 4));
					m.write<std::uint8_t>(cam + 0x75, m.u8(cam + 0x75) | 2);
				}
				fit(m.u32(cam + 4), p);
				for(unsigned at : { 0x18u, 0x1Cu, 0x28u, 0x30u }) m.write<float>(block + at, m.f32(m.u32(cam + 4) + at));
				desired = follow_point(block, false); // 0x487520 recomputes before the hold/progress tests
				store3(0x10, position); store3(0x1C, p);
				m.write<int>(cam + 0x60, 1);
				m.write<std::uint8_t>(cam + 0x75, (m.u8(cam + 0x75) | 4) & ~8u);
				if(obstruction) m.write<std::uint8_t>(cam + 0x75, m.u8(cam + 0x75) | 8);
				state.swing = 0;
			}
			void finish() {
				// 0x4876E0 restores both current and per-frame blocks.
				copy_block(m.u32(cam + 4), m.u32(cam + 8));
				copy_block(block, m.u32(cam + 8));
				m.write<std::uint8_t>(cam + 0x75, (m.u8(cam + 0x75) & ~10u) | 4);
				m.write<int>(cam + 0x60, 0);
				desired = follow_point(block, false);
			}
			void fallback(float dt) {
				// 0x486EA0 -> mode 2 -> 0x486F40: a view-only search can
				// relocate without a path from the old eye (a real recovery cut).
				if(auto found = search(globals & 4 ? 13 : 5, dt)) {
					state.trace.relocated = true;
					affine placement = *global_placement(m, state.super_object);
					for(unsigned k = 0; k < 3; ++k) placement.m[k][3] = found->first[k];
					set_global_placement(m, state.super_object, placement);
					position = found->first;
					state.previous_move = {};
					// 0x473480 also resets the mechanics aim and its carried step.
					// Keeping the pre-cut aim would briefly look up through the floor.
					state.aim = offset_point;
					state.previous_aim = {};
					state.carry_angle = 0;
					begin(found->first, false);
				}
			}
			vec3 follow(float dt) {
				float elapsed = float(vm.frame_ms());
				float still = m.f32(cam + 0x70);
				if(!(flags & (8 | 0x2000)) && !(m.u8(cam + 0x74) & 2)) {
					float timer = m.f32(cam + 0x68) + elapsed;
					m.write<float>(cam + 0x68, timer);
					if(timer >= count(0x78) * 1000 * dt) {
						m.write<float>(cam + 0x68, 0);
						// 0x4870C0 changes this frame's scenery gate only when
						// its route timer expires. Other triggers otherwise retain it.
						if(m.f32(cam + 0x6C) == 0) {
							if(still > dt * 10000) static_tests = false;
							else if(length(state.previous_move) > 0.0001f && length(sub(anchor, position)) > 0.0001f &&
								std::fabs(dot(normalised(state.previous_move), normalised(sub(anchor, position)))) > 0.9f) static_tests = false;
						}
						if(!route(anchor, position, true)) m.write<float>(cam + 0x6C, 0);
						else {
							float blocked = m.f32(cam + 0x6C) + elapsed;
							m.write<float>(cam + 0x6C, blocked);
							if(blocked > count(0x7E) * 1000 * dt) { m.write<float>(cam + 0x6C, 0); fallback(dt); }
							else if(rejected(desired, 3)) {
								m.write<float>(cam + 0x64, 0); m.write<float>(cam + 0x6C, 0);
								if(auto found = search(3, dt)) begin(found->first, true); else fallback(dt);
							}
						}
					}
				}
				// 0x486FA0: a stopped/colliding camera inside settle distance also searches.
				if(!(flags & 0x3000) && !(m.u8(cam + 0x74) & 2) &&
					length(sub(position, anchor)) <= m.f32(block + 0x20) &&
					((state.trace.report & 1) || (m.u8(cam + 0x74) & 4)) && rejected(desired, 3)) {
					m.write<float>(cam + 0x64, 0);
					if(auto found = search(flags & 8 ? 2 : 3, dt)) begin(found->first, true); else fallback(dt);
				}
				if(!(flags & 0x3000) && (state.trace.report & 3) == 3) {
					m.write<float>(cam + 0x64, 0);
					if(auto found = search(flags & 8 ? 2 : 3, dt)) begin(found->first, false); else fallback(dt);
				}
				// 0x487730: procedure 491 index 44 requests an improving search.
				if(mode() == 1) {
					float timer = m.f32(cam + 0x64) + elapsed;
					m.write<float>(cam + 0x64, timer);
					if(timer >= count(0x7A) * 1000 * dt && !(globals & 0x20) && m.u8(cam + 0x17C)) {
						m.write<float>(cam + 0x64, 0);
						if(auto found = search(11, dt)) {
							if(found->second == 0) finish(); else begin(found->first, false);
							m.write<std::uint8_t>(cam + 0x17C, 0);
						}
					}
				}
				if(may_leave(desired, dt)) m.write<std::uint8_t>(cam + 0x75, m.u8(cam + 0x75) & ~8u);
				if(mode() == 1 && (m.u8(cam + 0x75) & 8)) {
					// 0x485180: project the moving follow point onto the line
					// from the stored waypoint towards the tracked target.
					vec3 p = read3(m, cam + 0x1C), axis = normalised(sub(aim, p));
					if(length(axis) > 0 && length(tracked_motion()) > setting(0x58) && !(globals & 8))
						p = add(p, mul(axis, dot(sub(desired, p), axis)));
					fit(m.u32(cam + 4), p); store3(0x1C, p);
					return p;
				}
				return desired;
			}
			vec3 tracked_motion() const { return read3(m, cam + 0xCC); }
		};
	}

	static void solve_camera_impl(interpreter& vm, std::uint32_t super_object, bool offset_only, bool immediate_cut)
	{
		live_memory& m = vm.memory();
		std::uint32_t actor = vm.actor_of(super_object);
		std::uint32_t cam = actor && m.contains(actor + 0x10, 4) ? m.u32(actor + 0x10) : 0;
		if(!cam || !m.contains(cam, 0x80))
			return;
		// The block in force: the scripts fill the override block every frame (0x4140A0), so it is
		// the one to solve unless nothing backed it up and the current block holds the values.
		std::uint32_t block = m.u32(cam + (m.u8(cam + camera_field::override_active) & 1 ? camera_field::block_override : camera_field::block_current));
		if(!block || !m.contains(block, camera_field::block_size))
			return;
		std::uint32_t camera_transform = global_transform(m, super_object);
		if(!camera_transform)
			return;
		std::uint16_t flags_72 = m.read<std::uint16_t>(block + camera_field::flags_72);
		vec3 position = read3(m, camera_transform + 4);
		auto setting = [&](std::size_t at) {
			float f;
			std::memcpy(&f, &vm.camera().values[at], 4);
			return f;
		};

		// The target and its orientation (0x483E40). The exe averages up to eight tracked points on
		// the target; all of them start at its position, so the position is what we aim at.
		std::uint32_t target = target_super_object(vm, block);
		std::uint32_t target_transform = global_transform(m, target);
		camera_state& state = vm.camera_motion();
		state.trace = {};
		state.trace.focus = state.focus_pending;
		state.focus_pending = false;
		for(unsigned i = 0; i < 8; ++i) state.trace.block[i] = m.f32(block + 0x18 + i * 4);
		state.trace.flags70 = m.read<std::uint16_t>(block + 0x70);
		state.trace.flags72 = flags_72;
		state.trace.mode = m.read<std::int32_t>(cam + 0x60);
		auto word = [&](std::uint32_t at) { return m.contains(at, 4) ? m.u32(at) : 0u; };
		std::uint32_t dynamics = word(actor + 8), camera_base = dynamics ? word(dynamics) : 0;
		std::uint32_t camera_report = camera_base ? word(camera_base + 0x150) : 0;
		state.trace.report = camera_report && m.contains(camera_report + 0x168, 1) ? m.u8(camera_report + 0x168) : 0;
		std::memcpy(&state.trace.global_flags, &vm.camera().values[0xB0], 4);
		if(target != state.tracked_target && m.contains(cam, 0x158))
		{
			for(std::uint32_t i = 0; i < 8; i++)
				m.write<float>(cam + 0x138 + i * 4, 0);
			state.tracked_target = target;
			state.swing = 0;
			state.swing_gap = 0;
		}
		vec3 tracked_speed{};
		vec3 tracked_position{};
		bool have_tracked_position = false;
		// Procedure 444 (0x47DA40): shift the target's movement and position into the eight
		// samples, weighted by the frame duration. 0x414300 seeds empty samples at the target.
		if(target_transform && m.contains(cam, 0x158))
		{
			vec3 target_pos = read3(m, target_transform + 4);
			bool empty = true;
			for(std::uint32_t i = 0; i < 8; i++)
				empty = empty && m.f32(cam + 0x138 + i * 4) == 0;
			if(empty)
				for(std::uint32_t i = 0; i < 8; i++)
				{
					for(std::uint32_t k = 0; k < 3; k++)
					{
						m.write<float>(cam + 0x78 + i * 12 + k * 4, 0);
						m.write<float>(cam + 0xD8 + i * 12 + k * 4, target_pos[k]);
					}
					m.write<float>(cam + 0x138 + i * 4, 0.03f);
				}
			std::uint32_t target_actor = vm.actor_of(target);
			std::uint32_t dyn = target_actor && m.contains(target_actor + 8, 4) ? m.u32(target_actor + 8) : 0;
			std::uint32_t base = dyn && m.contains(dyn, 4) ? m.u32(dyn) : 0;
			std::uint32_t report = base && m.contains(base + 0x150, 4) ? m.u32(base + 0x150) : 0;
			if(!immediate_cut && report && m.contains(report, 0x158) && length(read3(m, report + 0x14C)) > 0)
			{
				for(std::uint32_t i = 0; i < 7; i++)
				{
					for(std::uint32_t k = 0; k < 3; k++)
						for(std::uint32_t start : { 0x78u, 0xD8u })
							m.write<float>(cam + start + i * 12 + k * 4, m.f32(cam + start + (i + 1) * 12 + k * 4));
					m.write<float>(cam + 0x138 + i * 4, m.f32(cam + 0x138 + (i + 1) * 4));
				}
				for(std::uint32_t k = 0; k < 3; k++)
				{
					m.write<float>(cam + 0xCC + k * 4, m.f32(report + 0x114 + k * 4));
					m.write<float>(cam + 0x12C + k * 4, m.f32(report + 0x14C + k * 4));
				}
				m.write<float>(cam + 0x154, 1.0f / 60.0f);
			}
			auto average = [&](std::uint32_t start, float threshold) {
				vec3 result{};
				float weight = 0;
				for(std::uint32_t i = 0; i < 8; i++)
				{
					float w = m.f32(cam + 0x138 + i * 4);
					vec3 v = read3(m, cam + start + i * 12);
					if(w != 0 && length(v) > 0)
					{
						result = add(result, mul(v, w));
						weight += w;
						if(weight >= threshold) break;
					}
				}
				return weight > 0 ? mul(result, 1.0f / weight) : vec3{};
			};
			tracked_speed = average(0x78, setting(0x50));
			tracked_position = average(0xD8, setting(0x4C));
			have_tracked_position = length(tracked_position) > 0;
			// A scripted cut can put the camera at the destination before its target is
			// teleported there. Discard the old samples in that case: following their
			// average would immediately undo the cut and send the camera back across
			// the level. This is an approximation pending the executable's full reset.
			if(target == vm.main_actor() && have_tracked_position &&
				length(sub(position, target_pos)) < 10.0f && length(sub(tracked_position, target_pos)) > 10.0f)
			{
				for(std::uint32_t i = 0; i < 8; i++)
				{
					for(std::uint32_t k = 0; k < 3; k++)
					{
						m.write<float>(cam + 0x78 + i * 12 + k * 4, 0);
						m.write<float>(cam + 0xD8 + i * 12 + k * 4, target_pos[k]);
					}
					m.write<float>(cam + 0x138 + i * 4, 0.03f);
				}
				tracked_position = target_pos;
				tracked_speed = {};
				state.look_at = add(target_pos, rotate(m, target_transform, read3(m, block + camera_field::vector_b)));
			}
		}
		vec3 aim{};
		if(target_transform)
			aim = add(have_tracked_position && !(flags_72 & 0x2) && !(m.u8(cam + 0x74) & 0x2) ? tracked_position : read3(m, target_transform + 4), rotate(m, target_transform, read3(m, block + camera_field::vector_b)));

		// The offset point (0x4840A0): vector A from the aim point, in the target's orientation, or
		// the vector a script fixed in the camera object (495).
		vec3 off{};
		if(flags_72 & fixed_vector_a)
			off = read3(m, cam + camera_object_field::vectors);
		else if(target_transform)
			off = add(aim, rotate(m, target_transform, read3(m, block + camera_field::vector_a)));

		if(offset_only) { state.off = off; return; }

		// 0x485810 maintains this timer even while a script fixes the eye.
		if(state.valid && !immediate_cut)
		{
			float still = m.f32(cam + 0x70);
			if(length(state.previous_move) >= setting(0x54)) still = 0;
			else
			{
				std::uint16_t frames;
				std::memcpy(&frames, &vm.camera().values[0x7C], 2);
				if(still < float(frames) * float(vm.frame_ms())) still += float(vm.frame_ms());
				m.write<std::uint8_t>(cam + 0x74, m.u8(cam + 0x74) | 4);
			}
			m.write<float>(cam + 0x70, still);
		}

		// The wanted position (0x4847D0).
		vec3 want{}, back{ 0, 0, 1 };
		if(flags_72 & fixed_position)
		{
			// A: a script fixed the position outright (496, 499).
			want = read3(m, cam + camera_object_field::vectors + 0x0C);
		}
		else if(!target_transform)
		{
			// B: nothing to look at, so stay where we are. The exe leaves the look-at and the offset
			// point at zero here, which would swing the camera round to face the world origin; no
			// game camera runs that way (its scripts always name a target or fix the position), so
			// keep the view instead, aimed at a point in front of the wanted position.
			want = position;
		}
		else
		{
			// C: on the sphere of the clamped distance around the aim point, in the direction the
			// block's angles ask for, measured from a reference axis (0x4847D0).
			vec3 d = sub(position, aim);
			float len = length(d);
			float near = m.f32(block + camera_field::min_distance), far = m.f32(block + camera_field::max_distance);
			float r = len;
			if(len < near)
				r = near;
			else if(len > far)
				r = far;
			r = std::max(r, 0.01f);
			float low = m.f32(block + camera_field::min_height), high = m.f32(block + camera_field::max_height);
			auto wanted_along = [&](const vec3& way) {
				vec3 w = add(aim, mul(way, r));
				w[2] = std::clamp(w[2], low, std::max(low, high));
				return w;
			};
			direction dir = fitted_direction(vm, cam, block, target_transform, d, flags_72, {}, immediate_cut || !(m.u8(cam + 0x75) & 8));
			state.trace.yaw = dir.yaw;
			state.trace.gap = dir.gap;
			back = dir.back;
			want = wanted_along(back);


		}
		if(!target_transform)
		{
			aim = front_point(m, camera_transform);
			off = aim;
		}

		// Ours: a look-at point that jumped far in one frame (the target was teleported, as Rayman
		// is to the portal when the level starts) cuts to the new view instead of travelling there.
		// The exe resets its camera when the target changes (0x413E30); how it handles a teleported
		// target isn't read yet.
		constexpr float teleported = 10;
		// Only for Rayman: the menu's camera follows a marker (node 7) that jumps from waypoint to
		// waypoint, and the real game pans there (user 2026-11-04), so other targets travel.
		bool cut = !immediate_cut && state.valid && target_transform && target == vm.main_actor() &&
			length(sub(aim, state.look_at)) > teleported &&
			length(sub(position, read3(m, target_transform + 4))) > teleported;
		// A cut need not travel from the old position. If its destination is hidden from the
		// target, search around the target for a clear landing. This is our approximation; the
		// executable's startup reset (0x485810) and obstacle transitions have different modes.
		if(cut && !(flags_72 & 0x3000) && !vm.world().empty() && vm.world().first_hit(aim, want))
		{
			float radius = camera_radius(m, super_object);
			vec3 offset = sub(want, aim);
			for(float scale : { 1.0f, 0.75f, 0.5f })
			{
				std::vector<vec3> candidates;
				for(int step = 0; step < 16; step++)
				{
					float angle = float(step) * pi / 8.0f;
					float cs = std::cos(angle), sn = std::sin(angle);
					candidates.push_back(add(aim, mul(vec3{ offset[0] * cs - offset[1] * sn,
						offset[0] * sn + offset[1] * cs, offset[2] }, scale)));
				}
				std::ranges::sort(candidates, [&](const vec3& a, const vec3& b) {
					return length(sub(a, want)) < length(sub(b, want));
				});
				bool found = false;
				for(const vec3& candidate : candidates)
					if(!vm.world().first_hit(aim, candidate) &&
						(radius <= 0 || length(sub(vm.world().slide_sphere(candidate, candidate, radius), candidate)) < 0.01f))
					{
						want = candidate;
						found = true;
						break;
					}
				if(found) break;
			}
		}
		if(state.trace_collisions && target_transform && !vm.world().empty())
		{
			vec3 anchor = add(read3(m, target_transform + 4), rotate(m, target_transform, read3(m, block + camera_field::vector_b)));
			state.trace.view_blocked = vm.world().first_hit(anchor, position).has_value();
			state.trace.wanted_blocked = vm.world().first_hit(anchor, want).has_value();
		}
		state.super_object = super_object;
		state.trace.collision_radius = camera_radius(m, super_object);
		if(immediate_cut)
		{
			// 0x47EDD0 calls 0x486EA0 / 0x486F40 directly, without the
			// ordinary solve's timers, swing or mechanics handoff.
			if(target_transform)
			{
				vec3 up = flags_72 & 0x4000 ? normalised(read3(m, cam + 0x4C)) : vec3{ 0, 0, 1 };
				vec3 ref = flags_72 & 0x4000 ? normalised(read3(m, cam + 0x40)) : normalised(read3(m, target_transform + 0x1C));
				if(length(up) < 0.5f) up = { 0, 0, 1 };
				if(length(ref) < 0.5f) ref = { 0, -1, 0 };
				vec3 anchor = add(read3(m, target_transform + 4), rotate(m, target_transform, read3(m, block + camera_field::vector_b)));
				obstacle_search search{ vm, m, state, cam, block, target_transform, anchor, aim, position, want, off, ref, up, flags_72, state.trace.global_flags };
				search.fallback(float(vm.frame_ms()) * 0.001f);
				m.write<std::uint16_t>(block + 0x70, m.read<std::uint16_t>(block + 0x70) | 5);
				m.write<std::uint8_t>(cam + 0x17C, 0);
			}
			return;
		}
		if(target_transform && state.valid && !cut && !(flags_72 & 0x2000) && !(m.u8(cam + 0x74) & 2))
		{
			vec3 up = flags_72 & 0x4000 ? normalised(read3(m, cam + 0x4C)) : vec3{ 0, 0, 1 };
			vec3 ref = flags_72 & 0x4000 ? normalised(read3(m, cam + 0x40)) : normalised(read3(m, target_transform + 0x1C));
			if(length(up) < 0.5f) up = { 0, 0, 1 };
			if(length(ref) < 0.5f) ref = { 0, -1, 0 };
			vec3 anchor = add(read3(m, target_transform + 4), rotate(m, target_transform, read3(m, block + camera_field::vector_b)));
			obstacle_search search{ vm, m, state, cam, block, target_transform, anchor, aim, position, want, off, ref, up, flags_72, state.trace.global_flags };
			float dt = float(vm.frame_ms()) * 0.001f;
			if(m.read<int>(cam + 0x60) <= 1) {
				// 0x485810: while moving, arriving at the fitted follow point
				// restores the spare block and resumes the ordinary follow solve.
				if(search.mode() == 1 && (search.globals & 0x10) && length(tracked_speed) > setting(0x58) && search.near(position, want)) {
					search.finish();
				}
				want = search.follow(dt);
				// Mode 2 may have changed the eye before mechanics runs.
				position = search.position;
			}
			state.trace.waypoint = search.mode() == 1;
		}
		else if((flags_72 & 0x3000) || (m.u8(cam + 0x74) & 2))
		{
			m.write<std::uint8_t>(cam + 0x75, m.u8(cam + 0x75) & ~12u);
		}
		state.trace.mode = m.read<int>(cam + 0x60);
		// The obstruction handlers precede the swing handler in 0x485810.
		if(target_transform && !(flags_72 & fixed_position) && !(m.u8(cam + 0x75) & 8) && m.read<int>(cam + 0x60) <= 1)
		{
			vec3 d = sub(position, aim);
			float near = m.f32(block + camera_field::min_distance), far = m.f32(block + camera_field::max_distance);
			float r = std::max(std::clamp(length(d), near, std::max(near, far)), 0.01f);
			float low = m.f32(block + camera_field::min_height), high = m.f32(block + camera_field::max_height);
			auto wanted_along = [&](const vec3& way) {
				vec3 w = add(aim, mul(way, r));
				w[2] = std::clamp(w[2], low, std::max(low, high));
				return w;
			};
			direction dir = camera_direction(m, cam, block, target_transform, d, flags_72);
			// 0x485760 / 0x4855D0: keep a chosen direction, or latch blocked until
			// the yaw gap becomes small or the block disables swinging.
			const float eighth = setting(0xA4);
			if(!(m.u8(cam + 0x74) & 2) && (std::fabs(dir.gap) < eighth || (flags_72 & 0x4)))
				state.swing = 0;
			else if(!(m.u8(cam + 0x74) & 2) && state.swing != -2)
			{
				if((state.trace.report & 1) && !(state.trace.global_flags & 1))
					state.swing = -2;
				else
				{
					if(state.swing_gap * dir.gap < 0 && std::fabs(dir.gap) < pi - eighth)
						state.swing = 0;
					if(state.swing == 0) state.swing_gap = dir.gap;
					vec3 anchor = add(read3(m, target_transform + 4), rotate(m, target_transform, read3(m, block + camera_field::vector_b)));
					auto try_step = [&](int sign) {
						float angle = wrap_two_pi(dir.yaw + sign * eighth);
						vec3 candidate_back = fitted_direction(vm, cam, block, target_transform, d, flags_72, angle).back;
						vec3 candidate = wanted_along(candidate_back);
						bool rejected = length(sub(candidate, anchor)) < m.f32(block + 0x20) + setting(0x6C);
						// 0x4853C0 requests these casts only in mode 1 with cam+0x75 bit 2.
						// 0x485FD0 samples rays around the segment. Our world has static level
						// triangles but not the executable's dynamic collision objects.
						if(!rejected && state.trace.mode == 1 && (m.u8(cam + 0x75) & 4))
						{

							auto count = [&](std::size_t offset) { std::uint16_t n; std::memcpy(&n, &vm.camera().values[offset], 2); return unsigned(n); };
							if(!(flags_72 & 8) && camera_route_obstructed(vm.world(), anchor, candidate, setting(0x3C), setting(0x40), count(0x76))) rejected = true;
							if(camera_route_obstructed(vm.world(), position, candidate, setting(0x34), setting(0x38), count(0x74))) rejected = true;
						}
						int n = state.trace.candidates++;
						state.trace.candidate[n] = candidate;
						state.trace.rejected[n] = rejected;
						if(!rejected) { back = candidate_back; want = candidate; }
						return !rejected;
					};
					int sign = state.swing == 0 ? (dir.gap > 0 ? 1 : -1) : state.swing;
					if(try_step(sign))
					{
						if(state.swing == 0) state.swing = sign;
					}
					else if(state.swing == 0 && std::fabs(dir.gap) >= pi - eighth && try_step(-sign))
						state.swing = -sign;
					else
						state.swing = -2;
				}
			}
		}

		m.write<std::uint8_t>(cam + 0x76, static_cast<std::uint8_t>(state.swing));
		m.write<float>(cam + 0x5C, state.swing_gap);
		state.trace.swing = state.swing;
		state.trace.swing_gap = state.swing_gap;
		state.trace.wanted = want;
		state.move = sub(want, position);
		state.look_at = aim;
		state.off = off;
		state.back = back;
		state.flags = m.read<std::uint16_t>(block + camera_field::flags_70);
		// 0x474420: a script-fixed camera position must use the direct position step,
		// without the mechanics' separate orbit around the target.
		if(flags_72 & fixed_position)
			state.flags |= keep_orientation;
		// The two direction smoothing divisors (0x474420): the camera settings' +0xA8/+0xAC
		// (0x4FF6D4/0x4FF6D8, 1 at start-up, 0x4138B0) become mechanics +0x26C/+0x27C, but stay 1
		// while the block snaps the position or keeps the orientation (+0x70 bits 0x01 and 0x04), while
		// it snaps the aim (bit 0x10), and while the target's tracked points stand still (0x473570 ->
		// 0x414420: their weighted average under 0.01). We have no tracked points, so having a target
		// stands in for points that move.
		state.position_smoothing = setting(0xA8);
		state.aim_smoothing = setting(0xAC);
		if(state.flags & (snap_position | keep_orientation))
			state.position_smoothing = 1;
		if(state.flags & aim_snap)
			state.aim_smoothing = 1;
		if(!target_transform)
		{
			state.position_smoothing = 1;
			state.aim_smoothing = 1;
		}
		// The three speeds (0x484B50): the block's own when a script fixed them (+0x72 bit 0x80) or
		// there is no target; otherwise worked out each frame from what is left to do, with the
		// camera settings' gains and caps (offsets from 0x4FF62C, set up at 0x4138C0). Tracked
		// target movement also supplies minimum position and aim speeds (0x484B50).
		const std::uint16_t flags_70 = m.read<std::uint16_t>(block + camera_field::flags_70);
		const bool fixed_speeds = (flags_72 & 0x80) || !target_transform;
		float position_speed = 0, turn_speed = 0;
		if(fixed_speeds || (flags_72 & 0x2000))
		{
			position_speed = m.f32(block + camera_field::speed);
			turn_speed = m.f32(block + camera_field::turn_speed);
		}
		else
		{
			// Position (0x484BCD): 3 x the distance still to go, at most 10 units/s; nothing while
			// the snap flag puts the camera there at once.
			if(!(flags_70 & 0x01))
				position_speed = std::max(std::min(setting(0x80) * length(sub(want, position)), setting(0x84)), setting(0x88) * length(tracked_speed));
			// Turn (0x484C67): 3 x the angle the camera must still swing round the look-at point,
			// at most 3 rad/s, slower while it is nearer than its boom length (+0x18) and faster
			// while it is more than a unit farther; the position speed when turning is held off.
			if(flags_70 & 0x04)
				turn_speed = position_speed;
			else
			{
				vec3 u = sub(aim, position), w = sub(aim, want);
				float lu = length(u), lw = length(w);
				if(lu <= 0)
					turn_speed = std::min(setting(0x8C) * position_speed, setting(0x90));
				else
				{
					float a = lw > 0 ? std::acos(std::clamp(dot(u, w) / (lu * lw), -1.0f, 1.0f)) : 0;
					turn_speed = std::min(setting(0x8C) * a, setting(0x90));
				}
				float t = m.f32(block + camera_field::min_distance) - lu;
				if(t > 1)
					turn_speed /= t;
				else if(t < -1)
					turn_speed *= -t;
			}
		}
		state.speed = position_speed;
		state.speed_ramp = std::max(m.f32(block + camera_field::speed_ramp), 0.001f);
		state.arrive = m.f32(block + camera_field::arrive);
		state.turn_speed = turn_speed;
		state.turn_ramp = std::max(m.f32(block + camera_field::turn_ramp), 0.001f);
		state.turn_arrive = m.f32(block + camera_field::turn_arrive);
		// Aim point (0x484F35): 4 x the distance it must still travel, at most 10 units/s.
		if(fixed_speeds)
			state.aim_speed = m.f32(block + camera_field::aim_speed);
		else if(flags_70 & 0x10)
			state.aim_speed = 0;
		else
			state.aim_speed = std::max(std::min(setting(0x98) * length(sub(state.valid ? state.aim : off, off)), setting(0x9C)), setting(0xA0) * length(tracked_speed));
		state.aim_ramp = std::max(m.f32(block + camera_field::aim_ramp), 0.001f);
		state.aim_arrive = m.f32(block + camera_field::aim_arrive);
		if(cut)
		{
			state.flags |= snap_position | keep_orientation;
			state.aim = off;
			state.previous_move = {};
			state.previous_aim = {};
			state.carry_angle = 0;
		}
		if(!state.valid)
		{
			// The mechanics starts its own aim point at the first solve's.
			state.aim = aim;
			state.previous_move = {};
			state.previous_aim = {};
			state.carry_angle = 0;
			state.valid = true;
		}
	}

	void solve_camera(interpreter& vm, std::uint32_t super_object, bool offset_only)
	{
		solve_camera_impl(vm, super_object, offset_only, false);
	}

	void cut_camera(interpreter& vm, std::uint32_t super_object)
	{
		solve_camera_impl(vm, super_object, false, true);
	}

	void move_camera(live_memory& m, std::uint32_t super_object, camera_state& state, double ms, const world_collision& world,
		const world_collision* objects)
	{
		std::uint32_t transform = global_transform(m, super_object);
		if(!state.valid || !transform)
			return;
		const double dt = ms / 1000.0;
		const vec3 position = read3(m, transform + 4);
		vec3 pos = position;

		// 1. The position: the exe's "step towards" (0x433B00, called at 0x433E46). Only a snap or a
		//    sub-tolerance move bypasses the limits. Even inside the speed cap, arrival and growth
		//    from last movement's length ([base+0x150]+0xF8) can shorten the step; bit 1 bypasses them.
		float maxstep = float(state.speed * dt);
		float len = length(state.move);
		if((state.flags & snap_position) || len <= 0.001f)
			pos = add(position, state.move);
		else
		{
			float step = std::min(len, maxstep);
			if(!(state.flags & 2) && state.arrive != 0)
			{
				float ease = len / state.arrive;
				if(ease < step && ease > 0.001f)
					step = ease;
			}
			if(!(state.flags & 2) && state.speed_ramp != 0)
			{
				float grows = maxstep / state.speed_ramp + length(state.previous_move);
				if(grows < step)
					step = grows;
			}
			pos = add(position, mul(state.move, step / len));
		}

		// 2. The orientation: the camera orbits the look-at point (0x433E6D..0x4340E3). The distance
		//    is the one after the position step, and turning the view by a limited angle puts the
		//    position back on that sphere, so the camera swings round the point it looks at.
		if(!(state.flags & keep_orientation))
		{
			vec3 to = sub(state.look_at, pos);
			float dist = length(to);
			if(dist > 1e-6f)
			{
				vec3 from = normalised(sub(state.look_at, position));
				vec3 v2 = mul(to, 1.0f / dist);
				vec3 axis = cross(from, v2);
				float sine = length(axis);
				if(sine > 1e-6f)
				{
					float angle = std::acos(std::max(-1.0f, std::min(1.0f, dot(from, v2))));
					axis = mul(axis, 1.0f / sine);
					// 0x433CD0: the turn a tick, bounded by a rate that builds on `carry_angle`.
					float most = float(state.turn_speed * dt);
					float step = std::min(angle, most);
					if(!(state.flags & 8) && state.turn_arrive != 0 && angle < state.turn_arrive * most)
						step = std::min(step, angle / state.turn_arrive);
					if(!(state.flags & 8) && state.turn_ramp != 0)
						step = std::min(step, most / state.turn_ramp + state.carry_angle);
					if(angle < 0.001f) step = angle;
					state.carry_angle = step;
					// 0x433CD0 accepts sub-tolerance angles outright, but a zero
					// allowed turn must retain the old direction (0x43405C).
					if(angle >= 0.001f)
						v2 = rotate_about(from, axis, step);
				}
				else
					state.carry_angle = 0;
				pos = sub(state.look_at, mul(v2, dist));
			}
		}

		// 3. The movement's direction (0x433930, called at 0x434139 with the mechanics' +0x26C): the
		//    whole tick's movement, step and orbit together, is turned a part of the way towards the
		//    last tick's direction and keeps its length, so a sideways push carries the old direction
		//    for a while. A block that both snaps the position and keeps the orientation skips it
		//    (0x434127). With the default divisor 1 it leaves the movement alone.
		if(!((state.flags & snap_position) && (state.flags & keep_orientation)))
			pos = add(position, smoothed_movement(sub(pos, position), state.previous_move, state.position_smoothing));

		// 5. Collision (0x434660): the camera's sphere moves from where it was to where the tick put
		//    it and slides along the level's surfaces, unless the block's +0x70 bit 0x40 turns it off.
		//    The turn carry becomes the angle the camera really swung round the look-at point. A
		//    snap (a fresh camera or a cut) is put in place, not moved there (the exe resets it, 0x413E30).
		auto word = [&](std::uint32_t at) { return m.contains(at, 4) ? m.u32(at) : 0u; };
		std::uint32_t actor = word(super_object + 4), dynamics = actor ? word(actor + 8) : 0;
		std::uint32_t base = dynamics ? word(dynamics) : 0, report = base ? word(base + 0x150) : 0;
		bool have_report = report && m.contains(report + 0x168, 1);
		if(have_report) m.write<std::uint8_t>(report + 0x168, m.u8(report + 0x168) & 0xFE);
		if(!(state.flags & 0x40) && !(state.flags & snap_position) && (!world.empty() || (objects && !objects->empty())))
		{
			if(float radius = camera_radius(m, super_object); radius > 0)
			{
				vec3 contact{};
				vec3 moved = world.slide_sphere(position, pos, radius, &contact);
				if(objects && !objects->empty())
				{
					vec3 object_contact{};
					vec3 object_move = objects->slide_sphere(position, moved, radius, &object_contact);
					if(object_move != moved)
					{
						if(length(contact) == 0) contact = object_contact;
						// Recheck scenery after sliding along an actor at a shared corner.
						moved = world.slide_sphere(position, object_move, radius);
					}
				}
				if(moved != pos)
				{
					if(have_report)
					{
						// 0x434660: bit 1 distinguishes a head-on obstruction while
						// travelling towards the aim point (0x49C710/0x49C714).
						vec3 direction = normalised(sub(pos, position));
						unsigned bits = m.u8(report + 0x168) & ~2u;
						if(dot(direction, contact) < -0.7f && dot(direction, normalised(sub(state.aim, position))) > 0.7f) bits |= 2;
						m.write<std::uint8_t>(report + 0x168, bits | 1);
					}
					pos = moved;
					vec3 from = normalised(sub(state.look_at, position)), to = normalised(sub(state.look_at, pos));
					state.carry_angle = std::acos(std::clamp(dot(from, to), -1.0f, 1.0f));
				}
			}
		}

		// 4. The aim point the camera looks at: the same kind of step towards the offset point
		//    (0x434230..0x434380, 0x433B00 at 0x434314), unless the scripts take it from the camera's
		//    own axes (bit 7): then it is a point in front of where the camera was (0x4341BF).
		if(state.flags & fixed_aim)
		{
			state.aim = sub(position, normalised(read3(m, transform + 0x1C)));
			state.previous_aim = {};
		}
		else
		{
			vec3 need = sub(state.off, state.aim);
			float want = length(need);
			vec3 step{};
			if((state.flags & aim_snap) || want <= 0.001f)
				step = need; // a snap or a movement smaller than 0.001 takes all of it (0x433B00)
			else
			{
				float reach = float(state.aim_speed * dt);
				if(!(state.flags & 0x20) && state.aim_arrive != 0 && want / state.aim_arrive > 0.001f)
					reach = std::min(reach, want / state.aim_arrive);
				if(!(state.flags & 0x20) && state.aim_ramp != 0)
					reach = std::min(reach, float(state.aim_speed * dt) / state.aim_ramp + length(state.previous_aim));
				reach = std::min(reach, want);
				step = mul(need, reach / want);
			}
			// 0x433930 again (0x43432B, the mechanics' +0x27C): the step's direction is turned
			// towards the last step's, and what comes out is kept for the next tick's ramp.
			step = smoothed_movement(step, state.previous_aim, state.aim_smoothing);
			state.aim = add(state.aim, step);
			state.previous_aim = step;
		}

		// What the next tick starts from (0x434660, the movement pass that follows 0x433D70): the
		// tick's movement, kept at [base+0x150]+0xF8. Its length is the step the step law 0x433B00
		// grows from, and its direction is what the next tick's smoothing turns towards.
		state.previous_move = sub(pos, position);

		// 6. Publish (0x435930, 0x435640): the second axis is the way back from the aim point to the
		//    camera, so -Y is what it looks along; the first stays as close to world Z as that allows.
		vec3 y = normalised(sub(pos, state.aim));
		if(length(y) < 1e-6f)
			y = { 0, 1, 0 };
		vec3 x = cross(y, vec3{ 0, 0, 1 });
		x = length(x) > 1e-4f ? normalised(x) : vec3{ 1, 0, 0 };
		vec3 z = cross(x, y);
		affine placement;
		for(int r = 0; r < 3; r++)
		{
			placement.m[std::size_t(r)][0] = x[std::size_t(r)];
			placement.m[std::size_t(r)][1] = y[std::size_t(r)];
			placement.m[std::size_t(r)][2] = z[std::size_t(r)];
			placement.m[std::size_t(r)][3] = pos[std::size_t(r)];
		}
		set_global_placement(m, super_object, placement);
	}
}
