#pragma once

#include <openrayman/game/live_memory.h>
#include <openrayman/geometry.h>

#include <array>
#include <cstdint>

namespace openrayman::game
{
	class interpreter;

	// Where the game camera wants to be, as its scripts' procedure 444 solves it each frame (0x485810
	// with the stages 0x483F90, 0x4840A0 and 0x4847D0), and what the mechanics that moves the camera
	// super object carries from one tick to the next (0x4359D0, the state it keeps in [[actor+8]+4]).
	// The numbers are the camera object's fields 0x474420 copies (0x20C..0x25E), so the exe's offsets
	// are in the comments. See docs/formats/camera.md.
	struct camera_state
	{
		// What the solve found.
		std::uint32_t super_object = 0; // the camera actor's super object, once a solve has run
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
		std::array<float, 3> aim{};     // the smoothed aim point (X+0x10)
		std::array<float, 3> previous_aim{}; // the last aim point step ([base+0x150]+0x130)
		// The direction smoothing divisors 0x474420 copies to the mechanics (+0x26C, +0x27C) from the
		// camera settings (+0xA8, +0xAC); 1 means no smoothing and is what the defaults give.
		float position_smoothing = 1;
		float aim_smoothing = 1;
	};

	// Procedure 444's solve (0x47DA40 -> 0x485810): the camera's scripts have filled the parameter
	// block that is in force, so work out the look-at point, the offset point and the wanted position,
	// and hand the numbers and the block's speeds to the mechanics through `vm.camera_motion()`.
	void solve_camera(interpreter& vm, std::uint32_t super_object);

	// One tick of the camera's mechanics (0x4359D0, its steps 1, 2, 4 and 6): step the camera's
	// position towards the wanted one, orbit it about the look-at point, ease the aim point, and
	// publish the result as the super object's global transform.
	void move_camera(live_memory& memory, std::uint32_t super_object, camera_state& state, double ms);
}
