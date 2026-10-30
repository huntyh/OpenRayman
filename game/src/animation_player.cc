#include <openrayman/game/animation_player.h>
#include <openrayman/animation.h>
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
		void play_events(live_memory& m, std::uint32_t data, std::uint32_t frame, bool to_end, const part_point& point)
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
				// 0x4189C0: the bit, and the point it came with: the world position of the event's
				// part (+8, an index into the parts, one per channel), posed at the event's frame.
				// Rayman's shoot animation arms marker 0 from his hand (part 11).
				m.write<std::uint8_t>(data + data_3d::markers, std::uint8_t(m.u8(data + data_3d::markers) | 1u << (id - 6)));
				std::array<float, 3> at = point ? point(reference, m.read<std::uint16_t>(e + event_frame), m.read<std::uint16_t>(e + event_part)) : std::array<float, 3>{};
				for(int k = 0; k < 3; k++)
					m.write<float>(data + data_3d::marker_point + std::uint32_t(k) * 4, at[std::size_t(k)]);
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

		// The motion the animation's frames carry, for the dynamics (base +0x54, 0x412060 via
		// 0x40FF90): each frame record names a vector (u16 +2, 0xFFFF none) in the bank. The frames
		// played this tick (after `before` up to the current one; only the current one when it
		// didn't advance or the state changed) are averaged and times the rate: units per second in
		// the actor's own axes. Rayman's ledge climb moves him up and onto the ledge with it.
		void animation_motion(live_memory& m, const animation_bank& bank, std::uint32_t actor, std::uint32_t data, std::uint32_t state_before, int before)
		{
			std::uint32_t dynamics = m.u32(actor + 8);
			std::uint32_t b = dynamics && m.contains(dynamics, 4) ? m.u32(dynamics) : 0;
			if(!b || !m.contains(b, 0x60))
				return;
			std::array<float, 3> sum{};
			std::uint32_t s = m.u32(data + data_3d::current_state);
			std::uint32_t reference = s ? m.u32(s + state::animation) : 0;
			std::uint16_t index = reference ? m.read<std::uint16_t>(reference + 0x18) : 0xFFFF;
			if(index < bank.animations.size())
			{
				const animation& a = bank.animations[index];
				int now = m.read<std::uint16_t>(data + data_3d::frame);
				int from = s == state_before && before < now ? before + 1 : now;
				int count = 0;
				for(int f = from; f <= now; f++, count++)
				{
					std::size_t at = std::size_t(a.start_frames) + std::size_t(f);
					if(f >= a.frames || at >= bank.frames.size())
						continue;
					std::uint16_t v = bank.frames[at].vector;
					if(v != 0xFFFF && v < bank.vectors.size())
						for(int i = 0; i < 3; i++)
							sum[std::size_t(i)] += bank.vectors[v][std::size_t(i)];
				}
				float rate = m.u8(data + data_3d::playback_rate);
				for(float& c : sum)
					c = count ? c / float(count) * rate : 0;
			}
			for(int i = 0; i < 3; i++)
				m.write<float>(b + 0x54 + std::uint32_t(i) * 4, sum[std::size_t(i)]);
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

	void play_animation(live_memory& m, std::uint32_t data, std::uint32_t ms, const part_point& point)
	{
		std::uint32_t s = m.u32(data + data_3d::current_state);
		std::uint32_t reference = s ? m.u32(s + state::animation) : 0;
		if(!reference)
			return;
		// 0x40E82A: a playback rate of 0 freezes the animation - its frame and marker bits stay.
		std::uint8_t rate = rate_of(m, data, s);
		if(!rate)
			return;
		// A state that played out with no passes left and nothing after it holds its last frame:
		// the exe sets +0x8E then (0x40E800) and stops updating the animation until a state starts
		// (0x40A5C0, 0x40FD30). Looping it showed the first frame of Rayman's ledge climb, the
		// hanging pose, for the ticks before his script moved on.
		if(m.u8(data + data_3d::finished) && !m.u32(s + state::after) && !m.u8(data + data_3d::loops))
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
		live_memory& m = vm.memory();
		// Cameras (actors with a camera object, actor +0x10) think after everyone else, so what
		// other actors ask of them this frame (the portal naming itself the target) is in place
		// when the camera commits.
		auto is_camera = [&](std::uint32_t self) {
			std::uint32_t actor = vm.actor_of(self);
			return actor && m.contains(actor + 0x10, 4) && m.u32(actor + 0x10) != 0;
		};
		auto think = [&](std::uint32_t self) {
			if(!on_error)
			{
				vm.think(self);
				return;
			}
			try
			{
				vm.think(self);
			}
			catch(const std::exception& e)
			{
				on_error(self, e);
			}
		};
		// Each actor thinks and then moves before the next one (0x412230: brain 0x4120D0, then
		// mechanics 0x412150), so the camera solves for where Rayman is this tick, not the last.
		auto advance = [&](std::uint32_t self) {
			std::uint32_t actor = vm.actor_of(self);
			std::uint32_t data = actor ? m.u32(actor) : 0;
			if(data && !vm.removed(self))
			{
				// Where a part of the actor is at a frame of an animation: the channel's pose, through
				// its parents, placed where the actor is (the parts' super objects the game keeps).
				auto part_at = [&](std::uint32_t reference, std::uint16_t frame, std::uint16_t part) {
					std::array<float, 3> out{};
					auto placed = global_placement(m, self);
					if(!placed)
						return out;
					affine world = *placed;
					const animation_bank* bank = vm.animations();
					std::uint16_t index = reference ? m.read<std::uint16_t>(reference + 0x18) : 0xFFFF;
					if(bank && index < bank->animations.size())
					{
						try
						{
							// The part is the channel with that id (Rayman's shoot animation 171: id 11, a
							// channel showing nothing, 1.49 ahead and 0.84 up at frame 4).
							std::vector<channel_pose> channels = pose(*bank, index, frame);
							auto found = std::ranges::find(channels, std::int16_t(part), &channel_pose::id);
							std::optional<std::size_t> c;
							if(found != channels.end())
								c = std::size_t(found - channels.begin());
							affine local;
							for(std::size_t steps = 0; c && *c < channels.size() && steps <= channels.size(); steps++, c = channels[*c].parent)
								local = channels[*c].local * local;
							if(found != channels.end())
								world = world * local;
						}
						catch(const std::exception&)
						{
						}
					}
					return std::array<float, 3>{ world.m[0][3], world.m[1][3], world.m[2][3] };
				};
				std::uint32_t state_before = m.u32(data + data_3d::current_state);
				int before = m.read<std::uint16_t>(data + data_3d::frame);
				play_animation(m, data, vm.frame_ms(), part_at);
				if(actor && vm.animations())
					animation_motion(m, *vm.animations(), actor, data, state_before, before);
			}
			if(actor && !vm.removed(self) && vm.mechanics_runs(self))
			{
				// The game camera is moved by its own mechanics (0x4359D0), not the actors' physics.
				camera_state& camera = vm.camera_motion();
				if(camera.valid && self == camera.super_object)
					move_camera(m, self, camera, vm.frame_ms(), vm.world());
				else
					move_actor(m, self, actor, vm.frame_ms(), vm.world());
			}
		};
		for(std::uint32_t self : super_objects)
			if(!is_camera(self))
			{
				think(self);
				advance(self);
			}
		// Actors the scripts created, after the level's own (the game links them into the dynamic
		// world as they come). One made this tick by an actor before them already thinks now.
		for(std::size_t i = 0; i < vm.generated().slots().size(); i++)
		{
			const generated_actors::slot& s = vm.generated().slots()[i];
			if(s.creator && !vm.removed(s.super_object))
			{
				std::uint32_t self = s.super_object;
				think(self);
				advance(self);
			}
		}
		for(std::uint32_t self : super_objects)
			if(is_camera(self))
			{
				think(self);
				advance(self);
			}
		// The pool's sweep (0x40C2F0, from 0x40ADEF): a generated actor whose custom bits carry
		// 0x20000 goes once its animation has played out with nothing after it (0x410310: 3D data
		// +0x8E, and no other state waiting). Rayman's shot ends this way after its fading state.
		for(std::uint32_t self : vm.generated().alive())
		{
			std::uint32_t actor = vm.actor_of(self);
			std::uint32_t standard = actor ? m.u32(actor + 0x04) : 0;
			std::uint32_t data = actor ? m.u32(actor) : 0;
			if(!standard || !data || !(m.u32(standard + 0x24) & 0x20000))
				continue;
			// Played out for good: the marker 0x40E800 sets only when no pass is left and nothing
			// comes after (our +0x8E also marks each pass of a loop, for conditions 77 and 106).
			std::uint32_t state = m.u32(data + data_3d::current_state);
			if(state && m.u8(data + data_3d::finished) && !m.u32(state + state::after) && !m.u8(data + data_3d::loops)
				&& state == m.u32(data + data_3d::next_state))
			{
				vm.remove(self);
				vm.generated().release(self);
			}
		}
		vm.particles().update(vm.clock() / 1000.0, vm.frame_ms() / 1000.0);
	}
}
