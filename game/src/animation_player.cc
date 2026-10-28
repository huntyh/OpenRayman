#include <openrayman/game/animation_player.h>
#include <openrayman/error.h>

#include <array>
#include <format>

namespace openrayman::game
{
	namespace
	{
		// A transition: next, target state, state to play on the way (0x4199B0, 0x42DE30, 0x4194B0).
		constexpr std::uint32_t transition_next = 0x00, transition_target = 0x04, transition_via = 0x08;
		// The animation reference: u16 frame count, u8 the animation's own speed (the engine does not
		// play by it: the state's rate does, see docs/formats/animation.md), events and their count.
		constexpr std::uint32_t reference_frames = 0x00, reference_events = 0x04, reference_event_count = 0x1A;
		// An event (12 bytes): descriptor, u16 frame at +6, u16 part at +8. Its descriptor: u32 id,
		// u8 type at +4, u8 start gate at +6 (0xFF: never from the playhead).
		constexpr std::uint32_t event_size = 12, event_frame = 6, event_part = 8;
		constexpr std::uint32_t descriptor_type = 4, descriptor_start_gate = 6;
		constexpr std::uint8_t event_marker = 3; // type 3, ids 6..13: arm marker id - 6 (0x41AFB0)

		// Plays the events of the current state's animation from the cursor (3D data +0x77) up to
		// `frame`, all of them with `to_end` (0x41ACB0, 0x41AE80). Only markers do something yet;
		// sounds (type 0) wait for milestone 3d. The exe also counts passes per event (the
		// descriptor's gates) before firing; here every pass fires.
		void play_events(live_memory& m, std::uint32_t data, std::uint32_t frame, bool to_end, const std::array<float, 3>& point)
		{
			std::uint32_t s = m.u32(data + data_3d::current_state);
			std::uint32_t reference = s ? m.u32(s + state::animation) : 0;
			std::uint32_t events = reference ? m.u32(reference + reference_events) : 0;
			if(!events)
				return;
			std::uint8_t count = m.u8(reference + reference_event_count);
			std::uint8_t i = m.u8(data + data_3d::event_cursor);
			for(; i < count; i++)
			{
				std::uint32_t e = events + i * event_size;
				if(!to_end && m.read<std::uint16_t>(e + event_frame) > frame)
					break;
				std::uint32_t descriptor = m.u32(e);
				if(!descriptor || m.u8(descriptor + descriptor_start_gate) == 0xFF || m.u8(descriptor + descriptor_type) != event_marker)
					continue;
				std::uint32_t id = m.u32(descriptor);
				if(id < 6 || id > 13)
					continue;
				// 0x4189C0: the bit, and the point it came with (the part's world position in the
				// game; the actor's here, until parts are placed in game memory).
				m.write<std::uint8_t>(data + data_3d::markers, std::uint8_t(m.u8(data + data_3d::markers) | 1u << (id - 6)));
				for(int k = 0; k < 3; k++)
					m.write<float>(data + data_3d::marker_point + std::uint32_t(k) * 4, point[std::size_t(k)]);
			}
			m.write<std::uint8_t>(data + data_3d::event_cursor, to_end ? 0 : i);
		}

		std::uint16_t frame_count(const live_memory& m, std::uint32_t s)
		{
			std::uint32_t reference = s ? m.u32(s + state::animation) : 0;
			return reference ? m.read<std::uint16_t>(reference + reference_frames) : 0;
		}

		// The frames per second the animation plays at (3D data +0xE0). Every state the game starts
		// takes its own rate there (0x40EC23 reads state +0x29, 0x418790 stores it; 0x40FD30 and
		// 0x40FE70 do the same); our memory image is the level's saved one, where the 3D data
		// constructor's 0 (0x418213) still stands, so the first use takes the state's rate.
		std::uint8_t rate_of(live_memory& m, std::uint32_t data, std::uint32_t s)
		{
			std::uint8_t rate = m.u8(data + data_3d::playback_rate);
			if(!rate && s)
				m.write<std::uint8_t>(data + data_3d::playback_rate, rate = m.u8(s + state::rate));
			return rate;
		}

		void start(live_memory& m, std::uint32_t data, std::uint32_t s)
		{
			m.write<std::uint32_t>(data + data_3d::current_state, s);
			m.write<std::uint32_t>(data + data_3d::next_state, s);
			m.write<std::uint8_t>(data + data_3d::playback_rate, m.u8(s + state::rate));
			// How many more times it plays before moving on (0x40ED75; counted down at 0x40EA4F).
			m.write<std::uint8_t>(data + data_3d::loops, m.u8(s + state::repeats));
			m.write<std::uint16_t>(data + data_3d::frame, 0);
			m.write<std::uint32_t>(data + data_3d::elapsed, 0);
			m.write<std::uint8_t>(data + data_3d::changed, 0);
			m.write<std::uint8_t>(data + data_3d::finished, 0);
			m.write<std::uint8_t>(data + data_3d::reached_end, 0);
			m.write<std::uint8_t>(data + data_3d::event_cursor, 0);
		}
	}

	bool change_state(live_memory& m, std::uint32_t data, std::uint32_t target, bool again, std::uint32_t clock, bool direct)
	{
		std::uint32_t current = m.u32(data + data_3d::current_state);
		if(!target || (target == m.u32(data + data_3d::requested_state) && !again))
			return true;
		// The game would take any number; here a script handing over something that isn't a state
		// (often a handler we don't have yet, reading 0 or a stray number) stops at the script.
		if(!m.contains(target, state::after + 4))
			throw format_error(std::format("0x{:X} is not a state", target));
		std::uint32_t play = target;
		if(!direct && current)
		{
			std::uint32_t via = 0;
			for(std::uint32_t t = m.u32(current + state::transitions); t; t = m.u32(t + transition_next))
			{
				if(m.u32(t + transition_target) == target)
				{
					via = m.u32(t + transition_via);
					break;
				}
			}
			if(via)
				play = via;
			else
			{
				std::uint32_t count = m.u32(current + state::forbidden_count);
				std::uint32_t f = m.u32(current + state::forbidden);
				for(std::uint32_t i = 0; f && i < count; i++, f = m.u32(f))
					if(m.u32(f + 4) == target)
						return false;
				if(m.u8(current + state::transitions_only) == 1)
					return false;
			}
		}
		m.write<std::uint32_t>(data + data_3d::previous_state, current);
		m.write<std::uint32_t>(data + data_3d::requested_state, target);
		m.write<std::uint32_t>(data + data_3d::changed_at, clock);
		start(m, data, play);
		return true;
	}

	void play_animation(live_memory& m, std::uint32_t data, std::uint32_t ms, const std::array<float, 3>& point)
	{
		std::uint32_t s = m.u32(data + data_3d::current_state);
		std::uint32_t reference = s ? m.u32(s + state::animation) : 0;
		if(!reference)
			return;
		// 0x40E82A: a playback rate of 0 freezes the animation - its frame and marker bits stay.
		std::uint8_t rate = rate_of(m, data, s);
		if(!rate)
			return;
		// Markers last one tick (0x40E841). "Ran out" (+0x8F) is recomputed every tick (0x40EF1B):
		// set while the last frame holds, for one tick, before the next pass or state starts.
		m.write<std::uint8_t>(data + data_3d::markers, 0);
		bool held_end = m.u8(data + data_3d::reached_end) != 0;
		m.write<std::uint8_t>(data + data_3d::reached_end, 0);
		std::uint16_t frames = frame_count(m, s);
		std::uint32_t elapsed = m.u32(data + data_3d::elapsed) + ms;
		// The frame the game shows is (clock 3D data +0xBC difference) x rate / 1000 (0x40E8E3);
		// here +0xC0 counts the same difference up, so the frame is the same without the phase.
		std::uint32_t frame = elapsed * rate / 1000;
		if(frames && frame >= frames)
		{
			if(!held_end)
			{
				// The tick it runs out: the rest of this pass's events, and the last frame holds so
				// the scripts see condition 106 with this state still current (0x40E800, 0x40E945).
				// Moving on at once let the next state's first tick look finished: Rayman's hand
				// return after a throw was cut after one tick.
				play_events(m, data, 0, true, point);
				m.write<std::uint8_t>(data + data_3d::finished, 1);
				m.write<std::uint8_t>(data + data_3d::reached_end, 1);
				m.write<std::uint32_t>(data + data_3d::elapsed, elapsed);
				m.write<std::uint16_t>(data + data_3d::frame, std::uint16_t(frames - 1));
				return;
			}
			// The tick after: the next pass or the next state.
			std::uint8_t loops = m.u8(data + data_3d::loops);
			std::uint32_t after = m.u32(s + state::after);
			if(loops)
				m.write<std::uint8_t>(data + data_3d::loops, std::uint8_t(loops - 1));
			else if(after && after != s)
			{
				start(m, data, after);
				play_events(m, data, 0, false, point);
				return;
			}
			// Loop: keep the time past the end.
			std::uint32_t length = std::uint32_t(frames) * 1000 / rate;
			elapsed = length ? elapsed % length : 0;
			frame = elapsed * rate / 1000;
		}
		m.write<std::uint32_t>(data + data_3d::elapsed, elapsed);
		m.write<std::uint16_t>(data + data_3d::frame, std::uint16_t(frame));
		play_events(m, data, frame, false, point);
	}

	void set_playback_rate(live_memory& m, std::uint32_t data, std::uint8_t rate)
	{
		// 0x418790: with a new rate above 1 the clock (+0xBC in the game, the time into the animation
		// here) is re-based by old / new, so the frame stays where it is; a rate of 1 or less only
		// gets stored.
		std::uint8_t old = rate_of(m, data, m.u32(data + data_3d::current_state));
		if(rate > 1)
			m.write<std::uint32_t>(data + data_3d::elapsed,
				std::uint32_t(std::uint64_t(m.u32(data + data_3d::elapsed)) * old / rate));
		m.write<std::uint8_t>(data + data_3d::playback_rate, rate);
	}
}

namespace openrayman::game
{
	std::optional<affine> global_placement(const live_memory& m, std::uint32_t super_object)
	{
		if(!super_object || !m.contains(super_object + 0x24, 4))
			return std::nullopt;
		std::uint32_t at = m.u32(super_object + 0x24);
		if(!at || !m.contains(at, 0x58))
			return std::nullopt;
		transform t;
		t.type = m.u32(at);
		for(int i = 0; i < 3; i++)
			t.position[std::size_t(i)] = m.f32(at + 4 + std::uint32_t(i) * 4);
		for(int i = 0; i < 9; i++)
		{
			t.rotation[std::size_t(i)] = m.f32(at + 0x10 + std::uint32_t(i) * 4);
			t.matrix[std::size_t(i)] = m.f32(at + 0x34 + std::uint32_t(i) * 4);
		}
		return affine::from(t);
	}

	void set_global_placement(live_memory& m, std::uint32_t super_object, const affine& a)
	{
		if(!super_object || !m.contains(super_object + 0x24, 4))
			return;
		std::uint32_t at = m.u32(super_object + 0x24);
		if(!at || !m.contains(at, 0x58))
			return;
		m.write<std::uint32_t>(at, 4);
		for(int r = 0; r < 3; r++)
		{
			m.write<float>(at + 4 + std::uint32_t(r) * 4, a.m[r][3]);
			for(int c = 0; c < 3; c++)
			{
				// Stored transposed (the game multiplies row vectors). Both copies get rotation and
				// scale together; nothing reads the first copy apart from axis directions.
				m.write<float>(at + 0x10 + std::uint32_t(c * 3 + r) * 4, a.m[r][c]);
				m.write<float>(at + 0x34 + std::uint32_t(c * 3 + r) * 4, a.m[r][c]);
			}
		}
	}

	void step(interpreter& vm, std::span<const std::uint32_t> super_objects, const step_error& on_error)
	{
		vm.next_tick();
		for(std::uint32_t self : super_objects)
		{
			if(!on_error)
			{
				vm.think(self);
				continue;
			}
			try
			{
				vm.think(self);
			}
			catch(const std::exception& e)
			{
				on_error(self, e);
			}
		}
		live_memory& m = vm.memory();
		for(std::uint32_t self : super_objects)
		{
			std::uint32_t actor = vm.actor_of(self);
			std::uint32_t data = actor ? m.u32(actor) : 0;
			if(data && !vm.removed(self))
			{
				std::array<float, 3> at{};
				if(auto placed = global_placement(m, self))
					at = { placed->m[0][3], placed->m[1][3], placed->m[2][3] };
				play_animation(m, data, vm.frame_ms(), at);
			}
			if(actor && !vm.removed(self))
			{
				// The game camera is moved by its own mechanics (0x4359D0), not the actors' physics.
				camera_state& camera = vm.camera_motion();
				if(camera.valid && self == camera.super_object)
					move_camera(m, self, camera, vm.frame_ms());
				else
					move_actor(m, self, actor, vm.frame_ms(), vm.world());
			}
		}
		vm.particles().update(vm.clock() / 1000.0, vm.frame_ms() / 1000.0);
	}
}
