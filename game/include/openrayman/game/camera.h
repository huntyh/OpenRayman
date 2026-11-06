#pragma once

#include <openrayman/game/live_memory.h>
#include <openrayman/game/mechanics.h>
#include <openrayman/geometry.h>

#include <array>
#include <cstdint>

namespace openrayman::game
{
	class interpreter;

	// Unproject the screen-object coordinates using the renderer's 4:3 projection.
	std::array<float, 3> screen_object_position(const affine& camera, const std::array<int, 3>& screen);
	// A procedure 409 object: where on screen (thousandths of the viewport, depth), the orientation
	// it has of its own (what its scripts or its saved transform give it), and what we last drew.
	struct screen_object
	{
		std::array<int, 3> at{};
		affine own, shown;
		bool placed = false;
	};
	// The whole placement of a screen object: that point, its own orientation turned with the game
	// camera (0x4052C0).
	affine screen_object_placement(const affine& camera, const std::array<int, 3>& screen, const affine& own);

	// Where the game camera wants to be, as its scripts' procedure 444 solves it each frame (0x485810
	// with the stages 0x483F90, 0x4840A0 and 0x4847D0), and what the mechanics that moves the camera
	// super object carries from one tick to the next (0x4359D0, the state it keeps in [[actor+8]+4]).
	// The numbers are the camera object's fields 0x474420 copies (0x20C..0x25E), so the exe's offsets
	// are in the comments. See docs/formats/camera.md.
	struct camera_state
	{
		struct solve_trace
		{
			std::array<float, 8> block{};
			std::uint16_t flags70 = 0, flags72 = 0;
			int mode = 0, swing = 0, candidates = 0;
			unsigned report = 0, global_flags = 0;
			bool waypoint = false, focus = false, view_blocked = false, wanted_blocked = false;
			float swing_gap = 0, yaw = 0, gap = 0;
			std::array<std::array<float, 3>, 2> candidate{};
			std::array<int, 2> rejected{};
			std::array<float, 3> wanted{};
		} trace;
		bool focus_pending = false, trace_collisions = false;

		// What the solve found.
		std::uint32_t super_object = 0; // the camera actor's super object, once a solve has run
		std::uint32_t tracked_target = 0; // reset the sampled target history when the target changes
		std::array<float, 3> move{};    // wanted position - camera position (object +0x218)
		std::array<float, 3> look_at{}; // the aim point the camera looks at (object +0x260)
		std::array<float, 3> off{};     // the aim point plus the block's first vector (object +0x20C)
		std::array<float, 3> back{};    // aim point -> camera, unit (object +0x270)
		std::uint32_t flags = 0;        // the block +0x70 option word (object +0x25E)
		float speed = 0;                // block +0x38: the position step's maximum, units/s (+0x240)
		float speed_ramp = 0;           // block +0x3C: how fast that step may grow (+0x244)
		float arrive = 0;               // block +0x40: the arrival ease (+0x248)
		float turn_speed = 0;           // block +0x44: the largest turn a tick, radians/s (+0x250)
		float turn_ramp = 0;            // block +0x48: how fast the turn may grow (+0x254)
		float turn_arrive = 0;          // block +0x4C: the turn's arrival ease (+0x258)
		float aim_speed = 0;            // block +0x50: the aim point's step, units/s (+0x224)
		float aim_ramp = 0;             // block +0x54: how fast it may grow (+0x228)
		float aim_arrive = 0;           // block +0x58: its arrival ease (+0x22C)

		// What the mechanics carries between ticks.
		bool valid = false;             // a solve has stored a wanted position
		std::array<float, 3> previous_move{}; // the last tick's movement, step and orbit together
		                                      // ([base+0x150]+0xF8, written by the movement pass 0x434660)
		float carry_angle = 0;          // the last tick's turn (X+0x0C)
		// Which way the camera swings round when its yaw is far from where the block wants it
		// (camera object +0x76: 0 not chosen, 1 the positive way, -1 the negative way), and the gap it
		// chose on (+0x5C). See solve_camera and 0x4855D0.
		int swing = 0;
		float swing_gap = 0;
		std::array<float, 3> aim{};     // the smoothed aim point (X+0x10)
		std::array<float, 3> previous_aim{}; // the last aim point step ([base+0x150]+0x130)
		// The direction smoothing divisors 0x474420 copies to the mechanics (+0x26C, +0x27C) from the
		// camera settings (+0xA8, +0xAC); 1 means no smoothing and is what the defaults give.
		float position_smoothing = 1;
		float aim_smoothing = 1;
		std::optional<std::array<float, 3>> obstacle_waypoint;
		std::array<float, 3> obstacle_target{};
		unsigned obstacle_retry = 0;
	};

	// Find a reachable position with a clear view of the target when a wall blocks the camera.
	// This is an approximation of the original's candidate search, pending a full trace.
	std::optional<std::array<float, 3>> camera_obstacle_waypoint(const world_collision& world,
		const std::array<float, 3>& target, const std::array<float, 3>& position,
		const std::array<float, 3>& desired, float radius);

	// Procedure 444's solve (0x47DA40 -> 0x485810): the camera's scripts have filled the parameter
	// block that is in force, so work out the look-at point, the offset point and the wanted position,
	// and hand the numbers and the block's speeds to the mechanics through `vm.camera_motion()`.
	void solve_camera(interpreter& vm, std::uint32_t super_object);

	// One tick of the camera's mechanics (0x4359D0, its steps 1, 2, 4 and 6): step the camera's
	// position towards the wanted one, orbit it about the look-at point, keep it out of the level's
	// surfaces (`world`, may be empty), ease the aim point, and publish the result as the super
	// object's global transform.
	void move_camera(live_memory& memory, std::uint32_t super_object, camera_state& state, double ms, const world_collision& world);
}
