#include <openrayman/game/animation_player.h>

namespace openrayman::game
{
	namespace
	{
		// A transition: next, target state, state to play on the way (0x4199B0, 0x42DE30, 0x4194B0).
		constexpr std::uint32_t transition_next = 0x00, transition_target = 0x04, transition_via = 0x08;
		// The animation reference: u16 frame count, u8 frames per second.
		constexpr std::uint32_t reference_frames = 0x00, reference_speed = 0x02;

		std::uint16_t frame_count(const live_memory& m, std::uint32_t s)
		{
			std::uint32_t reference = s ? m.u32(s + state::animation) : 0;
			return reference ? m.read<std::uint16_t>(reference + reference_frames) : 0;
		}

		void start(live_memory& m, std::uint32_t data, std::uint32_t s)
		{
			m.write<std::uint32_t>(data + data_3d::current_state, s);
			m.write<std::uint32_t>(data + data_3d::next_state, s);
			m.write<std::uint16_t>(data + data_3d::frame, 0);
			m.write<std::uint32_t>(data + data_3d::elapsed, 0);
			m.write<std::uint8_t>(data + data_3d::changed, 0);
			m.write<std::uint8_t>(data + data_3d::finished, 0);
			m.write<std::uint8_t>(data + data_3d::reached_end, 0);
		}
	}

	bool change_state(live_memory& m, std::uint32_t data, std::uint32_t target, bool forced, std::uint32_t clock)
	{
		std::uint32_t current = m.u32(data + data_3d::current_state);
		if(!target || (target == m.u32(data + data_3d::requested_state) && !forced))
			return true;
		std::uint32_t play = target;
		if(!forced && current)
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

	void play_animation(live_memory& m, std::uint32_t data, std::uint32_t ms)
	{
		std::uint32_t s = m.u32(data + data_3d::current_state);
		std::uint32_t reference = s ? m.u32(s + state::animation) : 0;
		if(!reference)
			return;
		// Recomputed every tick (0x40EDF0): set on the tick the animation runs out.
		m.write<std::uint8_t>(data + data_3d::reached_end, 0);
		std::uint16_t frames = frame_count(m, s);
		std::uint8_t speed = m.u8(reference + reference_speed);
		std::uint32_t elapsed = m.u32(data + data_3d::elapsed) + ms;
		std::uint32_t frame = elapsed * (speed ? speed : 30) / 1000;
		if(frames && frame >= frames)
		{
			m.write<std::uint8_t>(data + data_3d::finished, 1);
			std::uint8_t loops = m.u8(data + data_3d::loops);
			std::uint32_t after = m.u32(s + state::after);
			if(loops)
				m.write<std::uint8_t>(data + data_3d::loops, std::uint8_t(loops - 1));
			else if(after && after != s)
			{
				start(m, data, after);
				m.write<std::uint8_t>(data + data_3d::reached_end, 1);
				return;
			}
			m.write<std::uint8_t>(data + data_3d::reached_end, 1);
			// Loop: keep the time past the end.
			std::uint32_t length = std::uint32_t(frames) * 1000 / (speed ? speed : 30);
			elapsed = length ? elapsed % length : 0;
			frame = elapsed * (speed ? speed : 30) / 1000;
		}
		m.write<std::uint32_t>(data + data_3d::elapsed, elapsed);
		m.write<std::uint16_t>(data + data_3d::frame, std::uint16_t(frame));
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

	// Stand-in for the game's physics (milestone 3c): a speed the scripts set this tick
	// (procedures 176 and 182, dynamics +0x24, marked by bits 0xE0000 of +8) moves the actor
	// freely, in its own axes unless bit 0x4000 says world axes. No gravity, no collision yet, which
	// is what flying actors such as the fireflies need. The marks are cleared after each tick.
	void apply_speed(live_memory& m, std::uint32_t super_object, std::uint32_t actor, double ms)
	{
		std::uint32_t dynamics = m.u32(actor + 8);
		std::uint32_t base = dynamics && m.contains(dynamics, 4) ? m.u32(dynamics) : 0;
		if(!base || !m.contains(base, 0x1A8))
			return;
		std::uint32_t flags = m.u32(base + 8);
		if(!(flags & 0xE0000))
			return;
		m.write<std::uint32_t>(base + 8, flags & ~0xE0000u);
		std::uint32_t t = m.u32(super_object + 0x24);
		if(!t || !m.contains(t, 0x58))
			return;
		float v[3] = { m.f32(base + 0x24), m.f32(base + 0x28), m.f32(base + 0x2C) };
		float world[3] = { v[0], v[1], v[2] };
		if(!(flags & 0x4000))
		{
			for(int i = 0; i < 3; i++)
			{
				world[i] = 0;
				for(int axis = 0; axis < 3; axis++)
					world[i] += v[axis] * m.f32(t + 0x10 + std::uint32_t(axis * 3 + i) * 4);
			}
		}
		for(int i = 0; i < 3; i++)
		{
			std::uint32_t at = t + 4 + std::uint32_t(i) * 4;
			m.write<float>(at, m.f32(at) + float(world[i] * ms / 1000));
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
				play_animation(m, data, vm.frame_ms());
			if(actor && !vm.removed(self))
				apply_speed(m, self, actor, vm.frame_ms());
		}
		vm.particles().update(vm.clock() / 1000.0, vm.frame_ms() / 1000.0);
	}
}
