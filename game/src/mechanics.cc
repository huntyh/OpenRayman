#include <openrayman/game/mechanics.h>

#include <algorithm>
#include <cmath>

namespace openrayman::game
{
	namespace
	{
		// The dynamics base ([[actor+8]], 0x419B90).
		namespace base
		{
			constexpr std::uint32_t flags = 0x08, end_flags = 0x0C, gravity = 0x10;
			constexpr std::uint32_t imposed = 0x24, proposed = 0x30, speed = 0x3C;
			constexpr std::uint32_t contacts = 0x150, limit = 0x178, third = 0x190;
			constexpr std::uint32_t size = 0x1A8;
		}
		// Flags at base +8.
		constexpr std::uint32_t has_gravity = 0x4, has_limit = 0x200;
		constexpr std::uint32_t uses_imposed = 0xE0000, uses_proposed = 0x700000, uses_third = 0x3800000;
		// The resolvers 0x439730, 0x431A10, 0x43A260 (0x10, 0x20, 0x40) and Rayman's mechanics, which has
		// none of them but 0x2 (what resolves his is still to read).
		constexpr std::uint32_t collides = 0x2 | 0x10 | 0x20 | 0x40;
		constexpr std::uint32_t kept_after_tick = 0x5C003FFF;  // 0x4327D9
		constexpr std::uint32_t no_gravity = 0x8000;            // at base +0xC
		// The contact block (base +0x150, 0x16C bytes): +0 this tick's word, +4 the word scripts read.
		constexpr std::uint32_t contacts_size = 0x16C;
		constexpr std::uint32_t on_ground = 0x1;
		// How far a grounded actor follows the floor down (slopes, steps) instead of falling, and how
		// far up it climbs onto one. Ours: the exe works with the actor's collision spheres.
		constexpr float follow_down = 0.3f, climb_up = 0.5f, foot_radius = 0.5f;
		constexpr float floor_normal = 0.5f; // cos 60°, the support limit of 0x43A260

		using vec = std::array<float, 3>;

		// A mechanics (state +0x24): u32 kind at +0, flags at +4, gravity +8, slope +0x14 and, for
		// actors whose base allows it, a few more values. 0x435DF0 installs it at base +4 and, for
		// kind 1, copies it into the base (0x435B80).
		constexpr std::uint32_t state_mechanics = 0x24, current_mechanics = 0x04;

		void set_bit(live_memory& m, std::uint32_t at, std::uint32_t bit, bool on)
		{
			std::uint32_t v = m.u32(at);
			m.write<std::uint32_t>(at, on ? v | bit : v & ~bit);
		}

		void install_mechanics(live_memory& m, std::uint32_t b, std::uint32_t mech)
		{
			if(m.u32(mech) == 1)
			{
				std::uint32_t from = m.u32(mech + 4);
				// Bits 0x1..0x40 keep their place in base +8; the others move.
				for(std::uint32_t bit = 0x1; bit <= 0x40; bit <<= 1)
					set_bit(m, b + base::flags, bit, from & bit);
				for(std::uint32_t i = 0; i < 7; i++)
					set_bit(m, b + base::flags, 0x80u << i, from & (0x200u << i));
				set_bit(m, b + base::end_flags, 0x100, from & 0x80);
				set_bit(m, b + base::end_flags, 0x200, from & 0x100);
				set_bit(m, b + base::end_flags, 0x100000, from & 0x20000);
				m.write<float>(b + base::gravity, m.f32(mech + 8));
				float slope = m.f32(mech + 0x14);
				m.write<float>(b + 0x14, slope);
				m.write<float>(b + 0x18, std::sqrt(1.0f / (slope * slope + 1.0f)));
				std::uint32_t end = (m.u32(b + base::end_flags) & 0xFDDFFFFFu) | 0x10;
				m.write<std::uint32_t>(b + base::end_flags, end);
				if(end & 2)
				{
					m.write<std::uint32_t>(b + 0x1C, m.u32(mech + 0x0C));
					m.write<std::uint32_t>(b + 0x20, m.u32(mech + 0x10));
					for(std::uint32_t k = 0; k < 12; k += 4)
					{
						m.write<std::uint32_t>(b + 0x154 + k, m.u32(mech + 0x18 + k));
						m.write<std::uint32_t>(b + base::limit + k, m.u32(mech + 0x30 + k));
						m.write<std::uint32_t>(b + 0x1A8 + k, m.u32(b + 0xD4 + k));
					}
				}
				else if(end & 4)
					for(std::uint32_t k = 0; k < 12; k += 4)
						m.write<std::uint32_t>(b + 0x1DC + k, m.u32(mech + 0x24 + k));
			}
			m.write<std::uint32_t>(b + current_mechanics, mech);
		}

		vec read_vec(const live_memory& m, std::uint32_t at)
		{
			return { m.f32(at), m.f32(at + 4), m.f32(at + 8) };
		}

		void write_vec(live_memory& m, std::uint32_t at, const vec& v)
		{
			for(int i = 0; i < 3; i++)
				m.write<float>(at + std::uint32_t(i) * 4, v[std::size_t(i)]);
		}
	}

	world_collision::world_collision(std::vector<collision_triangle> triangles)
		: m_triangles(std::move(triangles))
	{
		for(std::uint32_t i = 0; i < m_triangles.size(); i++)
		{
			const collision_triangle& t = m_triangles[i];
			float lo[2], hi[2];
			for(int a = 0; a < 2; a++)
			{
				lo[a] = std::min({ t.corners[0][a], t.corners[1][a], t.corners[2][a] });
				hi[a] = std::max({ t.corners[0][a], t.corners[1][a], t.corners[2][a] });
			}
			for(int x = int(std::floor(lo[0] / cell)); x <= int(std::floor(hi[0] / cell)); x++)
				for(int y = int(std::floor(lo[1] / cell)); y <= int(std::floor(hi[1] / cell)); y++)
					m_cells[{ x, y }].push_back(i);
		}
	}

	std::optional<float> world_collision::floor_at(float x, float y, float top, float bottom) const
	{
		auto found = m_cells.find({ int(std::floor(x / cell)), int(std::floor(y / cell)) });
		if(found == m_cells.end())
			return std::nullopt;
		std::optional<float> best;
		for(std::uint32_t i : found->second)
		{
			const collision_triangle& t = m_triangles[i];
			if(t.normal[2] < floor_normal || (t.flags & surface::water))
				continue; // walls, and water, which actors sink into
			// Barycentric coordinates on the ground plane, then the height of the plane there.
			const vec3 &a = t.corners[0], &b = t.corners[1], &c = t.corners[2];
			float d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
			if(std::abs(d) < 1e-8f)
				continue;
			float u = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d;
			float v = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d;
			float w = 1 - u - v;
			if(u < -1e-5f || v < -1e-5f || w < -1e-5f)
				continue;
			float z = u * a[2] + v * b[2] + w * c[2];
			if(z <= top && z >= bottom && (!best || z > *best))
				best = z;
		}
		return best;
	}

	void move_actor(live_memory& m, std::uint32_t super_object, std::uint32_t actor, double ms, const world_collision& world)
	{
		std::uint32_t dynamics = m.u32(actor + 8);
		std::uint32_t b = dynamics && m.contains(dynamics, 4) ? m.u32(dynamics) : 0;
		if(!b || !m.contains(b, base::size) || ms <= 0)
			return;
		std::uint32_t t = m.u32(super_object + 0x24);
		if(!t || !m.contains(t, 0x58))
			return;
		// The mechanics of the actor's state, when it changed (0x412150, and after procedures 72..75
		// change the state, 0x472345).
		std::uint32_t data = m.u32(actor);
		std::uint32_t s = data && m.contains(data, 8) ? m.u32(data + 4) : 0;
		std::uint32_t mech = s && m.contains(s, state_mechanics + 4) ? m.u32(s + state_mechanics) : 0;
		if(mech && mech != m.u32(b + current_mechanics) && m.contains(mech, 0x3C))
			install_mechanics(m, b, mech);
		std::uint32_t flags = m.u32(b + base::flags);
		bool gravity = (flags & has_gravity) && !(m.u32(b + base::end_flags) & no_gravity);
		if(!gravity && !(flags & (uses_imposed | uses_proposed | uses_third)))
			return;
		float dt = float(ms / 1000);
		vec current = read_vec(m, t + 4), target = current;

		// A script speed (units per second) moves the target on the axes the script set (one bit
		// each, `x_bit` and the next two), in the actor's own axes unless its world bit is set
		// (procedures 182, 194, 207 set it: 0x46CCE1, 0x46CCB7, 0x46CD0B) (0x43E350, 0x43E680, 0x43E4E0).
		auto add_speed = [&](std::uint32_t at, std::uint32_t x_bit, std::uint32_t world_axes) {
			vec v = read_vec(m, b + at);
			for(int i = 0; i < 3; i++)
			{
				float world_i = v[std::size_t(i)];
				if(!(flags & world_axes))
				{
					world_i = 0;
					for(int axis = 0; axis < 3; axis++)
						world_i += v[std::size_t(axis)] * m.f32(t + 0x10 + std::uint32_t(axis * 3 + i) * 4);
				}
				if(flags & (x_bit << i))
					target[std::size_t(i)] += world_i * dt;
			}
		};
		if(flags & uses_proposed)
			add_speed(base::proposed, 0x100000, 0x8000);
		// Gravity (0x43DF40): from last tick's vertical speed, g in units per second squared, down.
		if(gravity)
			target[2] = current[2] + dt * (m.f32(b + base::speed + 8) - m.f32(b + base::gravity) * dt);
		if(flags & uses_imposed)
			add_speed(base::imposed, 0x20000, 0x4000);
		if(flags & uses_third)
			add_speed(base::third, 0x800000, 0x10000);
		// The per-axis speed limit (0x43EAC0).
		if(flags & has_limit)
			for(int i = 0; i < 3; i++)
			{
				float most = std::abs(m.f32(b + base::limit + std::uint32_t(i) * 4)) * dt;
				target[std::size_t(i)] = std::clamp(target[std::size_t(i)], current[std::size_t(i)] - most, current[std::size_t(i)] + most);
			}

		// The floor. The exe gathers contacts with the actor's collision spheres and picks the
		// nearest (0x43EEC0, 0x43F510); here the actor is a small disc at its origin, tested straight down.
		std::uint32_t block = m.u32(b + base::contacts);
		bool has_block = block && m.contains(block, contacts_size);
		bool landed = false;
		if((flags & collides) && !world.empty())
		{
			bool was_grounded = has_block && (m.u32(block + 4) & on_ground);
			float top = std::max(current[2], target[2]) + climb_up;
			float bottom = target[2] - (was_grounded ? follow_down : 0.0f);
			// The actor's feet cover a small circle: the highest floor under any of nine points of it.
			std::optional<float> floor;
			for(int k = 0; k < 9; k++)
			{
				float angle = float(k) * 0.785398f;
				float r = k == 8 ? 0.0f : foot_radius;
				if(auto f = world.floor_at(target[0] + r * std::cos(angle), target[1] + r * std::sin(angle), top, bottom); f && (!floor || *f > *floor))
					floor = f;
			}
			if(floor)
			{
				target[2] = *floor;
				landed = true;
			}
		}

		// The speed the scripts read back (0x436A30); landing drops the vertical part (0x43B337).
		vec speed;
		for(int i = 0; i < 3; i++)
			speed[std::size_t(i)] = (target[std::size_t(i)] - current[std::size_t(i)]) / dt;
		if(landed)
			speed[2] = 0;
		write_vec(m, b + base::speed, speed);
		write_vec(m, t + 4, target);
		if(has_block && (flags & collides))
		{
			m.write<std::uint32_t>(block, landed ? on_ground : 0);
			m.write<std::uint32_t>(block + 4, landed ? on_ground : 0); // published at the end of the tick
		}
		// The script speeds last one tick (0x4327D9).
		m.write<std::uint32_t>(b + base::flags, flags & kept_after_tick);
		for(std::uint32_t at = base::imposed; at < base::speed; at += 4)
			m.write<float>(b + at, 0.0f);
	}
}
