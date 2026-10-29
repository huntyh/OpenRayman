#include <openrayman/game/camera.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/game/interpreter.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// The game camera: the three stages of the solve its scripts run every frame (procedure 444 ->
// 0x485810) and the mechanics tick that moves the camera super object towards the result (0x4359D0).
// Names are ours; the addresses are the exe's. See docs/formats/camera.md.

namespace openrayman::game
{
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
		// where it is inside the window and goes to the nearer edge outside. (The exe also keeps a
		// pi/8-a-frame walker in the context, but the angle it applies is this one; the camera's
		// turn speed limits the swing.)
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
		vec3 camera_direction(const live_memory& m, std::uint32_t cam, std::uint32_t block, std::uint32_t target_transform, vec3 d, std::uint16_t flags_72)
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
			yaw = window_angle(yaw, m.f32(block + 0x28), hold ? 0 : m.f32(block + 0x2C));
			pitch = window_angle(pitch, m.f32(block + 0x30), hold ? 0 : m.f32(block + 0x34));
			// 0x484470: turn ref about up, then raise it about the axis across.
			vec3 v1 = rotate_about(ref, up, yaw);
			vec3 across = normalised(cross(v1, up));
			return length(across) > 0.5f ? normalised(rotate_about(v1, across, pitch)) : normalised(v1);
		}
	}

	void solve_camera(interpreter& vm, std::uint32_t super_object)
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

		// The target and its orientation (0x483E40). The exe averages up to eight tracked points on
		// the target; all of them start at its position, so the position is what we aim at.
		std::uint32_t target = target_super_object(vm, block);
		std::uint32_t target_transform = global_transform(m, target);
		vec3 aim{};
		if(target_transform)
			aim = add(read3(m, target_transform + 4), rotate(m, target_transform, read3(m, block + camera_field::vector_b)));

		// The offset point (0x4840A0): vector A from the aim point, in the target's orientation, or
		// the vector a script fixed in the camera object (495).
		vec3 off{};
		if(flags_72 & fixed_vector_a)
			off = read3(m, cam + camera_object_field::vectors);
		else if(target_transform)
			off = add(aim, rotate(m, target_transform, read3(m, block + camera_field::vector_a)));

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
			back = camera_direction(m, cam, block, target_transform, d, flags_72);
			float near = m.f32(block + camera_field::min_distance), far = m.f32(block + camera_field::max_distance);
			float r = len;
			if(len < near)
				r = near;
			else if(len > far)
				r = far;
			r = std::max(r, 0.01f);
			want = add(aim, mul(back, r));
			float low = m.f32(block + camera_field::min_height), high = m.f32(block + camera_field::max_height);
			if(want[2] < low)
				want[2] = low;
			else if(want[2] > high)
				want[2] = high;
		}
		if(!target_transform)
		{
			aim = front_point(m, camera_transform);
			off = aim;
		}

		camera_state& state = vm.camera_motion();
		// Ours: a look-at point that jumped far in one frame (the target was teleported, as Rayman
		// is to the portal when the level starts) cuts to the new view instead of travelling there.
		// The exe resets its camera when the target changes (0x413E30); how it handles a teleported
		// target isn't read yet.
		constexpr float teleported = 10;
		bool cut = state.valid && target_transform && length(sub(aim, state.look_at)) > teleported;
		state.super_object = super_object;
		state.move = sub(want, position);
		state.look_at = aim;
		state.off = off;
		state.back = back;
		state.flags = m.read<std::uint16_t>(block + camera_field::flags_70);
		auto setting = [&](std::size_t at) {
			float f;
			std::memcpy(&f, &vm.camera().values[at], 4);
			return f;
		};
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
		// camera settings' gains and caps (offsets from 0x4FF62C, set up at 0x4138C0). The exe adds
		// floors from the tracked points' average, which is zero unless scripts plant points.
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
				position_speed = std::min(setting(0x80) * length(sub(want, position)), setting(0x84));
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
			state.aim_speed = std::min(setting(0x98) * length(sub(state.valid ? state.aim : off, off)), setting(0x9C));
		state.aim_ramp = std::max(m.f32(block + camera_field::aim_ramp), 0.001f);
		state.aim_arrive = m.f32(block + camera_field::aim_arrive);
		if(cut)
		{
			state.flags |= snap_position;
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

	void move_camera(live_memory& m, std::uint32_t super_object, camera_state& state, double ms)
	{
		std::uint32_t transform = global_transform(m, super_object);
		if(!state.valid || !transform)
			return;
		const double dt = ms / 1000.0;
		const vec3 position = read3(m, transform + 4);
		vec3 pos = position;

		// 1. The position: the exe's "step towards" (0x433B00, called at 0x433E46). A snap flag or a
		//    step that fits takes the whole movement; otherwise a maximum step, an arrival ease and a
		//    rate that builds on the last movement's length ([base+0x150]+0xF8) bound it.
		float maxstep = float(state.speed * dt);
		float len = length(state.move);
		if((state.flags & snap_position) || len <= 0.001f || len <= maxstep)
			pos = add(position, state.move);
		else
		{
			float step = maxstep;
			if(state.arrive != 0)
			{
				float ease = len / state.arrive;
				if(ease < step && ease > 0.001f)
					step = ease;
			}
			if(state.speed_ramp != 0)
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
					if(state.turn_arrive != 0 && angle < state.turn_arrive * most)
						step = std::min(step, angle / state.turn_arrive);
					if(state.turn_ramp != 0)
						step = std::min(step, most / state.turn_ramp + state.carry_angle);
					state.carry_angle = step;
					if(step > 0)
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
				if(state.aim_arrive != 0)
					reach = std::min(reach, want / state.aim_arrive);
				if(state.aim_ramp != 0)
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
