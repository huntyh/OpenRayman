#include <openrayman/game/mechanics.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace openrayman::game
{
	namespace
	{
		// The dynamics base ([[actor+8]], 0x419B90).
		namespace base
		{
			constexpr std::uint32_t flags = 0x08, end_flags = 0x0C, gravity = 0x10;
			constexpr std::uint32_t animation_motion = 0x54;
			constexpr std::uint32_t imposed = 0x24, proposed = 0x30, speed = 0x3C, previous_position = 0x7C;
			constexpr std::uint32_t contacts = 0x150, limit = 0x178, third = 0x190;
			constexpr std::uint32_t size = 0x1A8;
		}
		// Flags at base +8.
		constexpr std::uint32_t has_animation_motion = 0x1, has_gravity = 0x4, has_limit = 0x200, has_inertia = 0x400;
		constexpr std::uint32_t uses_imposed = 0xE0000, uses_proposed = 0x700000, uses_third = 0x3800000;
		// The resolvers 0x439730, 0x431A10, 0x43A260 (0x10, 0x20, 0x40) and Rayman's mechanics, which has
		// none of them but 0x2 (what resolves his is still to read).
		constexpr std::uint32_t collides = 0x2 | 0x10 | 0x20 | 0x40;
		constexpr std::uint32_t kept_after_tick = 0x5C003FFF;  // 0x4327D9
		constexpr std::uint32_t no_gravity = 0x8000;            // at base +0xC
		// The contact block (base +0x150, 0x16C bytes): +0 this tick's word, +4 the word scripts read.
		constexpr std::uint32_t contacts_size = 0x16C;
		constexpr std::uint32_t on_ground = 0x1;
		constexpr std::uint32_t in_water = 0x40;   // slot 4 (0x43F940)
		constexpr std::uint32_t against_wall = 0x4; // slot 2 (condition 28)
		constexpr std::uint32_t water_slot = 4, wall_slot = 2;
		// After the two words come six 0x28-byte contact slots (0 the tick's first contact, 1 ground,
		// 2 wall, 3 the primary contact, 4 water, 5). An entry holds its surface's outward unit
		// normal at +4, the point it was touched at +0x10 and the surface's game material at +0x20
		// (0x43F940, 0x479260).
		constexpr std::uint32_t slots_at = 0x08, slot_size = 0x28;
		constexpr std::uint32_t slot_normal = 0x04, slot_point = 0x10, slot_material = 0x20;
		// +0x1C is the actor's own zone that touched (its game material): conditions 34..37 test its
		// collide flags (0x48A4F0).
		constexpr std::uint32_t slot_own = 0x1C;
		constexpr std::uint16_t ledge_probe = 0x8, grabbable = 0x4; // collide flags
		// How far a grounded actor follows the floor down (slopes, steps) instead of falling, and how
		// far up it climbs onto one. Ours: the exe works with the actor's collision spheres.
		constexpr float follow_down = 0.3f, climb_up = 0.5f, foot_radius = 0.5f;
		// How tall the cylinder walls push out of is, above the step height.
		constexpr float body_height = 1.8f;
		constexpr float wall_touch = 0.05f; // how near a wall counts as touching it
		constexpr float floor_normal = 0.5f; // cos 60°, the support limit of 0x43A260

		using vec = std::array<float, 3>;

		// The part of a triangle between two heights (a convex polygon of up to five corners, empty when
		// the triangle lies wholly above or below): a wall leaning over the floor only stands where
		// it crosses the body, not over its whole footprint (the overhang at the Learn_10 cave mouth).
		std::vector<vec> height_slice(const collision_triangle& t, float bottom, float top)
		{
			std::vector<vec> poly(t.corners.begin(), t.corners.end());
			auto clip = [&](float level, bool keep_above) {
				std::vector<vec> out;
				for(std::size_t i = 0; i < poly.size(); i++)
				{
					const vec &a = poly[i], &b = poly[(i + 1) % poly.size()];
					bool a_in = keep_above ? a[2] >= level : a[2] <= level;
					bool b_in = keep_above ? b[2] >= level : b[2] <= level;
					if(a_in)
						out.push_back(a);
					if(a_in != b_in)
					{
						float f = (level - a[2]) / (b[2] - a[2]);
						out.push_back({ a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, level });
					}
				}
				poly = std::move(out);
			};
			clip(bottom, true);
			if(!poly.empty())
				clip(top, false);
			return poly;
		}

		// The point of a convex polygon's ground-plane footprint nearest (x, y): (x, y) itself when
		// that is inside it, otherwise the nearest point of its edges.
		void closest_in_footprint(const std::vector<vec>& poly, float x, float y, float& px, float& py)
		{
			bool positive = false, negative = false;
			for(std::size_t i = 0; i < poly.size(); i++)
			{
				const vec &a = poly[i], &b = poly[(i + 1) % poly.size()];
				float side = (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
				positive |= side > 0;
				negative |= side < 0;
			}
			if(poly.size() >= 3 && !(positive && negative))
			{
				px = x;
				py = y;
				return;
			}
			float d_min = 0;
			for(std::size_t i = 0; i < poly.size(); i++)
			{
				const vec &a = poly[i], &b = poly[(i + 1) % poly.size()];
				float ex = b[0] - a[0], ey = b[1] - a[1];
				float len = ex * ex + ey * ey;
				float f = len > 0 ? std::clamp(((x - a[0]) * ex + (y - a[1]) * ey) / len, 0.0f, 1.0f) : 0.0f;
				float cx = a[0] + ex * f, cy = a[1] + ey * f;
				float d = (cx - x) * (cx - x) + (cy - y) * (cy - y);
				if(i == 0 || d < d_min)
				{
					d_min = d;
					px = cx;
					py = cy;
				}
			}
		}

		// The highest of `candidates` that `accepts` allows under (x, y), with its height in
		// [bottom, top]: barycentric coordinates on the ground plane, then the height of the plane
		// there (shared by floor_at and water_at).
		template<typename Accept>
		std::optional<world_collision::surface_hit> highest_surface(const std::vector<collision_triangle>& triangles,
			const std::vector<std::uint32_t>& candidates, float x, float y, float top, float bottom, Accept accepts)
		{
			std::optional<world_collision::surface_hit> best;
			for(std::uint32_t i : candidates)
			{
				const collision_triangle& t = triangles[i];
				if(!accepts(t))
					continue;
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
				if(z <= top && z >= bottom && (!best || z > best->height))
					best = world_collision::surface_hit{ z, t.material, t.normal };
			}
			return best;
		}

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

	std::optional<world_collision::surface_hit> world_collision::floor_at(float x, float y, float top, float bottom) const
	{
		return highest_surface(m_triangles, under(x, y), x, y, top, bottom, [](const collision_triangle& t) {
			return t.normal[2] >= floor_normal && !(t.flags & surface::no_contact); // walls, water, 0x8000
		});
	}

	std::optional<world_collision::segment_hit> world_collision::first_hit(const std::array<float, 3>& start, const std::array<float, 3>& end, std::uint16_t any_of) const
	{
		// Every solid triangle the segment crosses (Moller-Trumbore, both faces), keeping the
		// nearest. The segments the scripts cast are short, so all triangles are tried.
		const vec d = { end[0] - start[0], end[1] - start[1], end[2] - start[2] };
		std::optional<segment_hit> best;
		float nearest = 1.0f;
		for(const collision_triangle& t : m_triangles)
		{
			if((t.flags & surface::no_contact) || (any_of && !(t.flags & any_of)))
				continue;
			const vec e1 = { t.corners[1][0] - t.corners[0][0], t.corners[1][1] - t.corners[0][1], t.corners[1][2] - t.corners[0][2] };
			const vec e2 = { t.corners[2][0] - t.corners[0][0], t.corners[2][1] - t.corners[0][1], t.corners[2][2] - t.corners[0][2] };
			const vec p = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
			const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
			if(std::abs(det) < 1e-9f)
				continue;
			const vec s = { start[0] - t.corners[0][0], start[1] - t.corners[0][1], start[2] - t.corners[0][2] };
			const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
			if(u < 0.0f || u > 1.0f)
				continue;
			const vec q = { s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0] };
			const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det;
			if(v < 0.0f || u + v > 1.0f)
				continue;
			const float along = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
			if(along < 0.0f || along > nearest)
				continue;
			nearest = along;
			best = segment_hit{ { start[0] + d[0] * along, start[1] + d[1] * along, start[2] + d[2] * along }, t.normal, t.material };
		}
		return best;
	}

	namespace
	{
		vec vsub(const vec& a, const vec& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
		vec vadd(const vec& a, const vec& b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
		vec vmul(const vec& a, float f) { return { a[0] * f, a[1] * f, a[2] * f }; }
		float vdot(const vec& a, const vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
		vec vcross(const vec& a, const vec& b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
		float vlen(const vec& a) { return std::sqrt(vdot(a, a)); }

		// The point of a triangle nearest to p (by its regions: corners, edges, face).
		vec closest_on_triangle(const vec& p, const vec& a, const vec& b, const vec& c)
		{
			vec ab = vsub(b, a), ac = vsub(c, a), ap = vsub(p, a);
			float d1 = vdot(ab, ap), d2 = vdot(ac, ap);
			if(d1 <= 0 && d2 <= 0)
				return a;
			vec bp = vsub(p, b);
			float d3 = vdot(ab, bp), d4 = vdot(ac, bp);
			if(d3 >= 0 && d4 <= d3)
				return b;
			float vc = d1 * d4 - d3 * d2;
			if(vc <= 0 && d1 >= 0 && d3 <= 0)
				return vadd(a, vmul(ab, d1 / (d1 - d3)));
			vec cp = vsub(p, c);
			float d5 = vdot(ab, cp), d6 = vdot(ac, cp);
			if(d6 >= 0 && d5 <= d6)
				return c;
			float vb = d5 * d2 - d1 * d6;
			if(vb <= 0 && d2 >= 0 && d6 <= 0)
				return vadd(a, vmul(ac, d2 / (d2 - d6)));
			float va = d3 * d6 - d5 * d4;
			if(va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
				return vadd(b, vmul(vsub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
			float denom = 1.0f / (va + vb + vc);
			return vadd(a, vadd(vmul(ab, vb * denom), vmul(ac, vc * denom)));
		}
	}

	std::optional<world_collision::segment_hit> world_collision::sphere_touch(const std::array<float, 3>& centre, float radius, std::uint16_t flags, const std::function<bool(const segment_hit&)>& accept) const
	{
		std::optional<segment_hit> best;
		float nearest = radius;
		for(int x = int(std::floor((centre[0] - radius) / cell)); x <= int(std::floor((centre[0] + radius) / cell)); x++)
			for(int y = int(std::floor((centre[1] - radius) / cell)); y <= int(std::floor((centre[1] + radius) / cell)); y++)
			{
				auto it = m_cells.find({ x, y });
				if(it == m_cells.end())
					continue;
				for(std::uint32_t i : it->second)
				{
					const collision_triangle& t = m_triangles[i];
					if((t.flags & flags) != flags || (t.flags & surface::no_contact))
						continue;
					vec q = closest_on_triangle(centre, t.corners[0], t.corners[1], t.corners[2]);
					// Surfaces are one-sided: only a sphere on the front of one touches it, so a probe
					// coming at a ledge's top from below (Rayman jumping past it) doesn't grab.
					if(vdot(vsub(centre, q), t.normal) <= 0)
						continue;
					float l = vlen(vsub(centre, q));
					if(l <= nearest && (!accept || accept(segment_hit{ q, t.normal, t.material })))
					{
						nearest = l;
						best = segment_hit{ q, t.normal, t.material };
					}
				}
			}
		return best;
	}

	std::array<float, 3> world_collision::slide_sphere(const std::array<float, 3>& start, const std::array<float, 3>& end, float radius) const
	{
		if(m_triangles.empty() || radius <= 0)
			return end;
		// The solid triangles near the whole movement.
		std::vector<std::uint32_t> near;
		{
			float lo[2], hi[2];
			for(int a = 0; a < 2; a++)
			{
				lo[a] = std::min(start[a], end[a]) - radius;
				hi[a] = std::max(start[a], end[a]) + radius;
			}
			for(int x = int(std::floor(lo[0] / cell)); x <= int(std::floor(hi[0] / cell)); x++)
				for(int y = int(std::floor(lo[1] / cell)); y <= int(std::floor(hi[1] / cell)); y++)
					if(auto it = m_cells.find({ x, y }); it != m_cells.end())
						near.insert(near.end(), it->second.begin(), it->second.end());
			std::ranges::sort(near);
			near.erase(std::unique(near.begin(), near.end()), near.end());
			std::erase_if(near, [&](std::uint32_t i) { return (m_triangles[i].flags & surface::no_contact) != 0; });
		}
		if(near.empty())
			return end;
		// The deepest overlap of the sphere at p: how far it is inside, and the way out.
		auto overlap = [&](const vec& p, vec& out) {
			float deepest = 0;
			for(std::uint32_t i : near)
			{
				const collision_triangle& t = m_triangles[i];
				vec q = closest_on_triangle(p, t.corners[0], t.corners[1], t.corners[2]);
				vec d = vsub(p, q);
				float l = vlen(d);
				if(l < radius && radius - l > deepest)
				{
					deepest = radius - l;
					out = l > 1e-5f ? vmul(d, 1.0f / l) : t.normal;
				}
			}
			return deepest;
		};
		constexpr float margin = 0.02f; // 0x49C71C / 0x49C724
		// The clearance is not a minimum movement: rejecting small steps can keep the camera's
		// acceleration at zero after it stops at a wall.
		constexpr float epsilon = 1e-6f;
		vec pos = start;
		vec normal{};
		if(float depth = overlap(pos, normal); depth > 0)
			pos = vadd(pos, vmul(normal, depth + margin));
		vec target = end;
		const vec first = vsub(end, start);
		const float first_length = vlen(first);
		vec previous_normal{};
		for(int hit = 0; hit < 5; hit++)
		{
			vec move = vsub(target, pos);
			float length = vlen(move);
			if(length < epsilon)
				break;
			// The first point along the move where the sphere touches something: steps no longer
			// than a quarter of the radius, then halving.
			float step = std::max(radius * 0.25f, 0.01f);
			int steps = int(std::ceil(length / step));
			float free = 1, blocked = -1;
			for(int i = 1; i <= steps; i++)
			{
				float f = float(i) / float(steps);
				if(overlap(vadd(pos, vmul(move, f)), normal) > 0)
				{
					blocked = f;
					free = float(i - 1) / float(steps);
					break;
				}
			}
			if(blocked < 0)
			{
				pos = target;
				break;
			}
			for(int i = 0; i < 8; i++)
			{
				float mid = (free + blocked) * 0.5f;
				if(overlap(vadd(pos, vmul(move, mid)), normal) > 0)
					blocked = mid;
				else
					free = mid;
			}
			overlap(vadd(pos, vmul(move, blocked)), normal);
			// Stop 0.02 short of the surface.
			float advance = std::max(0.0f, free * length - margin);
			pos = vadd(pos, vmul(move, advance / length));
			// The rest of the movement along the surface, or along the crease with the last one.
			vec rest = vsub(target, pos);
			vec slide;
			vec crease = vcross(previous_normal, normal);
			if(hit > 0 && vlen(crease) > 1e-3f)
			{
				crease = vmul(crease, 1.0f / vlen(crease));
				slide = vmul(crease, vdot(rest, crease));
			}
			else
				slide = vsub(rest, vmul(normal, vdot(rest, normal)));
			previous_normal = normal;
			// A slide turned more than 150 degrees from the movement goes nowhere (0x49C720).
			float slide_length = vlen(slide);
			if(slide_length < epsilon || (first_length > epsilon && vdot(slide, first) / (slide_length * first_length) < std::cos(2.6179938f)))
				break;
			target = vadd(pos, slide);
		}
		return pos;
	}

	std::optional<world_collision::surface_hit> world_collision::water_at(float x, float y, float top, float bottom) const
	{
		// Water triangles are flat surfaces (0x4000); a steep one is a waterfall's wall, not a pond
		// (0x437E60 compares the actor's Z with the water entry's own height, 0x4B72D4).
		return highest_surface(m_triangles, under(x, y), x, y, top, bottom, [](const collision_triangle& t) {
			return (t.flags & surface::water) && std::abs(t.normal[2]) >= floor_normal;
		});
	}

	const std::vector<std::uint32_t>& world_collision::under(float x, float y) const
	{
		static const std::vector<std::uint32_t> none;
		auto found = m_cells.find({ int(std::floor(x / cell)), int(std::floor(y / cell)) });
		return found == m_cells.end() ? none : found->second;
	}

	std::array<float, 2> world_collision::wall_push(float x, float y, float bottom, float top, float radius) const
	{
		if(radius <= 0 || top <= bottom)
			return { 0.0f, 0.0f };
		// Every cell the cylinder covers, not only the one under its axis.
		std::vector<std::uint32_t> walls;
		for(int cx = int(std::floor((x - radius) / cell)); cx <= int(std::floor((x + radius) / cell)); cx++)
			for(int cy = int(std::floor((y - radius) / cell)); cy <= int(std::floor((y + radius) / cell)); cy++)
			{
				auto found = m_cells.find({ cx, cy });
				if(found != m_cells.end())
					walls.insert(walls.end(), found->second.begin(), found->second.end());
			}
		std::sort(walls.begin(), walls.end());
		walls.erase(std::unique(walls.begin(), walls.end()), walls.end());
		float start_x = x, start_y = y;
		// A few passes: pushed out of one wall, the cylinder may now be in the next.
		for(int pass = 0; pass < 3; pass++)
		{
			bool pushed = false;
			for(std::uint32_t i : walls)
			{
				const collision_triangle& t = m_triangles[i];
				if(std::abs(t.normal[2]) >= floor_normal || (t.flags & surface::no_contact))
					continue; // floors, ceilings, and water (actors swim through it) or surfaces without contact
				std::vector<vec> slice = height_slice(t, bottom, top);
				if(slice.size() < 2)
					continue; // it doesn't meet the cylinder's height
				float px, py;
				closest_in_footprint(slice, x, y, px, py);
				// Out of the wall: from its nearest point towards the axis.
				float dx = x - px, dy = y - py;
				float d = std::sqrt(dx * dx + dy * dy);
				if(d >= radius)
					continue; // the axis is far enough from this wall
				float amount = radius - d;
				if(d < 1e-6f)
				{
					// The axis is in the wall: leave the way the triangle faces.
					dx = t.normal[0];
					dy = t.normal[1];
					d = std::sqrt(dx * dx + dy * dy);
					if(d < 1e-6f)
						continue;
					amount = radius;
				}
				x += dx / d * amount;
				y += dy / d * amount;
				pushed = true;
			}
			if(!pushed)
				break;
		}
		return { x - start_x, y - start_y };
	}

	namespace
	{
		// The spheres of an actor's kind-1 zone list (collide set +8, the list the exe's sphere cast
		// gathers: 0x494290 -> 0x417870): each zone's first element when it is spheres (kind 7), its
		// first entry (radius, game material, vertex index, a word that is 5 for the zones in use and 2
		// for the others, by what the real game's contact report shows of Rayman's) and the vertex.
		struct zone_sphere
		{
			vec centre{};               // in the actor's own axes
			float radius = 0;
			std::uint32_t material = 0; // the zone's game material: its collide flags say what it is for
			bool in_use = false;
		};
		std::vector<zone_sphere> zone_spheres(const live_memory& m, std::uint32_t actor)
		{
			std::vector<zone_sphere> out;
			auto word = [&](std::uint32_t at) { return m.contains(at, 4) ? m.u32(at) : 0u; };
			std::uint32_t set = word(actor + 0x14);
			std::uint32_t head = set ? word(set + 8) : 0;
			for(std::uint32_t node = head ? word(head) : 0, i = 0; node && i < 32; node = word(node), i++)
			{
				std::uint32_t zone = word(node + 4);
				std::uint32_t vertices = zone ? word(zone) : 0;
				std::uint32_t kinds = zone ? word(zone + 0x10) : 0, data = zone ? word(zone + 0x14) : 0;
				if(!kinds || !data || !m.contains(kinds, 2) || m.read<std::int16_t>(kinds) != 7)
					continue;
				std::uint32_t entry = word(word(data));
				if(!entry || !m.contains(entry, 0x10))
					continue;
				zone_sphere z;
				z.radius = m.f32(entry);
				z.material = m.u32(entry + 4);
				z.in_use = m.u32(entry + 0xC) == 5;
				std::uint32_t at = vertices + std::uint32_t(m.read<std::uint16_t>(entry + 8)) * 12;
				if(vertices && m.contains(at, 12))
					z.centre = read_vec(m, at);
				out.push_back(z);
			}
			return out;
		}

		// A game material's collide flags (+0xC collide material, its u16 at +2; 0x42DB30, 0x42DDD0).
		std::uint16_t material_flags(const live_memory& m, std::uint32_t material)
		{
			std::uint32_t collide = material && m.contains(material + 0xC, 4) ? m.u32(material + 0xC) : 0;
			return collide && m.contains(collide + 2, 2) ? m.read<std::uint16_t>(collide + 2) : 0;
		}

		// How far walls keep the actor away: the radius of its first kind-1 sphere. Rayman's is 0.8,
		// centred 0.8 above his feet (his body: collide flags 0x4001), and the real game leaves him
		// that far from the wall by the Learn_10 portal. The exe takes the zones the scripts switched
		// on (0x40DBD0); the first one stands in for them until those are read. Others keep foot_radius.
		float body_radius(const std::vector<zone_sphere>& zones)
		{
			float r = zones.empty() ? 0.0f : zones.front().radius;
			return r > 0.05f && r < 3.0f ? r : foot_radius;
		}
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
		// End flag 0x80: the transform was set from outside. The dynamics takes it as it is, drops its
		// speed and clears the flag (0x431AD0). Left set, it also blocks turns (procedure 136,
		// 0x432EE0); the level data starts Rayman with it.
		if(std::uint32_t end = m.u32(b + base::end_flags); end & 0x80)
		{
			m.write<std::uint32_t>(b + base::end_flags, end & ~0x80u);
			write_vec(m, b + base::speed, vec{});
		}
		std::uint32_t flags = m.u32(b + base::flags);
		// The fast mechanics (end flag 0x200, from mechanics bit 0x100; 0x431AD0's first branch): the
		// stored speed (+0x3C, world axes) carries the actor on, x dt, and an imposed speed replaces
		// the axes it names. No gravity, inertia or floor: Rayman's shot flies this way and watches
		// for walls with its own casts. The speed stays what it was unless something imposed one.
		if(m.u32(b + base::end_flags) & 0x200)
		{
			const float dt = float(ms / 1000);
			vec speed = read_vec(m, b + base::speed);
			if(flags & uses_imposed)
			{
				vec imposed = read_vec(m, b + base::imposed);
				const bool world_axes = flags & 0x4000;
				std::array<vec, 3> axes{};
				for(std::uint32_t k = 0; k < 3; k++)
				{
					axes[k] = read_vec(m, t + 0x10 + k * 12);
					if(float l = vlen(axes[k]); l > 0)
						axes[k] = vmul(axes[k], 1 / l);
				}
				vec local = speed;
				if(!world_axes)
					for(std::size_t k = 0; k < 3; k++)
						local[k] = vdot(speed, axes[k]);
				for(std::size_t k = 0; k < 3; k++)
					if(flags & (0x20000u << k))
						local[k] = imposed[k];
				if(world_axes)
					speed = local;
				else
				{
					speed = {};
					for(std::size_t k = 0; k < 3; k++)
						for(std::size_t i = 0; i < 3; i++)
							speed[i] += local[k] * axes[k][i];
				}
			}
			vec target = read_vec(m, t + 4);
			for(std::size_t i = 0; i < 3; i++)
				target[i] += speed[i] * dt;
			write_vec(m, b + base::speed, speed);
			write_vec(m, t + 4, target);
			write_vec(m, b + base::previous_position, target);
			m.write<std::uint32_t>(b + base::flags, flags & kept_after_tick);
			for(std::uint32_t at = base::imposed; at < base::speed; at += 4)
				m.write<float>(b + at, 0.0f);
			return;
		}
		bool gravity =(flags & has_gravity) && !(m.u32(b + base::end_flags) & no_gravity);
		if(!gravity && !(flags & (has_animation_motion | uses_imposed | uses_proposed | uses_third)))
			return;
		float dt = float(ms / 1000);
		vec current = read_vec(m, t + 4), target = current;
		// Where this mechanics left the actor last tick (the exe keeps it in the dynamics' previous
		// matrix, +0x78, position at +0x7C). Somewhere else means a script put it there (a teleport,
		// procedure 200): that isn't a move into a wall, so walls don't push it this tick. The exe's
		// collision sweeps along the move (0x43EEC0) and its teleport resets both matrices (0x432DA0);
		// the Learn_10 portal holds Rayman against the tablet's wall this way, and the real game only
		// pushes him off it once he is let go.
		vec left = read_vec(m, b + base::previous_position);
		const bool placed = std::abs(left[0] - current[0]) > 1e-4f || std::abs(left[1] - current[1]) > 1e-4f || std::abs(left[2] - current[2]) > 1e-4f;

		// A script speed (units per second) sets the axes of this tick's move the script chose (one
		// bit each, `x_bit` and the next two), replacing what gravity or an earlier speed made of
		// them, in the actor's own axes unless its world bit is set (procedures 182, 194, 207 set it:
		// 0x46CCE1, 0x46CCB7, 0x46CD0B) (0x43E350, 0x43E42A..0x43E451, 0x43E680, 0x43E4E0).
		// The actor's own axes (unit length), and moves turned into them and back.
		std::array<vec, 3> axes{};
		for(int k = 0; k < 3; k++)
		{
			axes[std::size_t(k)] = read_vec(m, t + 0x10 + std::uint32_t(k) * 12);
			vec& a = axes[std::size_t(k)];
			float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
			if(l > 0)
				a = { a[0] / l, a[1] / l, a[2] / l };
		}
		auto to_local = [&](const vec& d) {
			vec in{};
			for(int k = 0; k < 3; k++)
				for(int i = 0; i < 3; i++)
					in[std::size_t(k)] += d[std::size_t(i)] * axes[std::size_t(k)][std::size_t(i)];
			return in;
		};
		auto to_world = [&](const vec& d) {
			vec out{};
			for(int k = 0; k < 3; k++)
				for(int i = 0; i < 3; i++)
					out[std::size_t(i)] += d[std::size_t(k)] * axes[std::size_t(k)][std::size_t(i)];
			return out;
		};
		auto set_move = [&](const vec& d) {
			for(int i = 0; i < 3; i++)
				target[std::size_t(i)] = current[std::size_t(i)] + d[std::size_t(i)];
		};
		auto move = [&] {
			return vec{ target[0] - current[0], target[1] - current[1], target[2] - current[2] };
		};

		auto add_speed = [&](std::uint32_t at, std::uint32_t x_bit, std::uint32_t world_axes) {
			vec v = read_vec(m, b + at);
			bool local = !(flags & world_axes);
			vec d = local ? to_local(move()) : move();
			for(int k = 0; k < 3; k++)
				if(flags & (x_bit << k))
					d[std::size_t(k)] = v[std::size_t(k)] * dt;
			set_move(local ? to_world(d) : d);
		};
		// The animation's own motion (0x437A40): base +0x54, units per second in the actor's axes,
		// which the animation player fills from the frames it played (see animation_player.cc).
		// Rayman's ledge climb lifts him onto the ledge this way.
		if(flags & has_animation_motion)
		{
			vec v = read_vec(m, b + base::animation_motion), d = move(), w = to_world(v);
			for(int i = 0; i < 3; i++)
				d[std::size_t(i)] += w[std::size_t(i)] * dt;
			set_move(d);
		}
		if(flags & uses_proposed)
			add_speed(base::proposed, 0x100000, 0x8000);
		// Inertia (0x43DF70 -> 0x43DDA0): per axis in the actor's own axes, the move keeps the share
		// f of last tick's (speed x dt) and takes the rest from the one asked for. The state's
		// factors (+0x154..) become f = 1 - (1 - k)^4 (0x4370A0; its frame-time correction is for
		// ticks other than 1/60 s). Rayman's k = 0.4 keeps 87%: the real game's portal hop starts at
		// 1.3 of the 10 units/s asked for and loses 13% a frame (tools/r2live.py). Last tick's move leaves out the carrying platform's
		// (+0x1B4) unless end flag 0x10 says otherwise; the exe rounds the result to 0.0001 units.
		if(flags & has_inertia)
		{
			vec last = read_vec(m, b + base::speed);
			for(float& c : last)
				c *= dt;
			if(!(m.u32(b + base::end_flags) & 0x10))
			{
				vec carried = read_vec(m, b + 0x1B4);
				for(int i = 0; i < 3; i++)
					last[std::size_t(i)] -= carried[std::size_t(i)];
			}
			vec wanted = to_local(move());
			last = to_local(last);
			vec d{};
			for(int k = 0; k < 3; k++)
			{
				float f = m.f32(b + 0x154 + std::uint32_t(k) * 4);
				float keep = 1 - f;
				f = f <= 0 ? 0.0f : f >= 1 ? f : 1 - keep * keep * keep * keep;
				d[std::size_t(k)] = wanted[std::size_t(k)] + (last[std::size_t(k)] - wanted[std::size_t(k)]) * f;
			}
			set_move(to_world(d));
		}
		// Gravity (0x43DF40): from last tick's vertical speed, g in units per second squared, down.
		if(gravity)
			target[2] = current[2] + dt * (m.f32(b + base::speed + 8) - m.f32(b + base::gravity) * dt);
		if(flags & uses_imposed)
			add_speed(base::imposed, 0x20000, 0x4000);
		if(flags & uses_third)
			add_speed(base::third, 0x800000, 0x10000);
		// The per-axis speed limit (0x43EAC0), in the actor's own axes: the move is turned into them,
		// each component clipped, and turned back.
		if(flags & has_limit)
		{
			vec d = to_local(move());
			for(int k = 0; k < 3; k++)
			{
				float most = std::abs(m.f32(b + base::limit + std::uint32_t(k) * 4)) * dt;
				d[std::size_t(k)] = std::clamp(d[std::size_t(k)], -most, most);
			}
			set_move(to_world(d));
		}

		// Holding on to walls (flag 0x40 without 0x10: 0x431AD0 picks the resolver 0x43A260; Rayman
		// spread between two pillars, behaviour 27). The move loses its part along the wall normal
		// held last tick (+0x1CC), then the nearest wall carrying collide flag 0x40 (not 0x8000) within
		// H of the body is held: steep (|n.z| under base +0x18) and not behind him (n . Y at least
		// -0.707, 0x49C760). He is turned to face it (0x4394A0: Y = n, X = n x up, Z = X x n, the
		// position moved by 0.9 x (old Z - new Z) so he turns about his middle). With no such wall
		// left he stays put and end flag 0x10000 says so (condition 80 lets him drop). Walls without
		// 0x40, such as the one joining the pillars, only block. H is 0.75 with end flag 0x10, else
		// the move's length x tan 60 degrees, at least 0.45 (0x49C7B8, 0x49C7B0, 0x49C7A0). Ours
		// looks with a sphere at body height instead of sweeping his zones between two positions.
		if((flags & 0x40) && !(flags & 0x10) && !world.empty() && !placed)
		{
			vec n = read_vec(m, b + 0x1CC);
			vec d = move();
			if(float along = vdot(d, n); vlen(n) > 0.5f)
				for(int i = 0; i < 3; i++)
					d[std::size_t(i)] -= n[std::size_t(i)] * along;
			set_move(d);
			const float h = (m.u32(b + base::end_flags) & 0x10) ? 0.75f : std::max(0.45f, vlen(d) * 1.7320508f);
			const float steep = m.f32(b + 0x18);
			const vec y_axis = axes[1];
			auto holdable = [&](const world_collision::segment_hit& s) {
				return std::abs(s.normal[2]) < steep && vdot(s.normal, y_axis) >= -0.707f;
			};
			const vec centre = { target[0], target[1], target[2] + 0.9f };
			if(auto wall = world.sphere_touch(centre, body_radius(zone_spheres(m, actor)) + h, 0x40, holdable))
			{
				const vec& w = wall->normal;
				write_vec(m, b + 0x1CC, w);
				if(std::abs(w[0] - y_axis[0]) >= 0.001f || std::abs(w[1] - y_axis[1]) >= 0.001f || std::abs(w[2] - y_axis[2]) >= 0.001f)
				{
					const vec x = { w[1], -w[0], 0.0f };
					const vec z = { x[1] * w[2] - x[2] * w[1], x[2] * w[0] - x[0] * w[2], x[0] * w[1] - x[1] * w[0] };
					for(int i = 0; i < 3; i++)
						target[std::size_t(i)] += 0.9f * (axes[2][std::size_t(i)] - z[std::size_t(i)]);
					const vec turned[3] = { x, w, z };
					for(std::uint32_t k = 0; k < 3; k++)
					{
						float scale = vlen(read_vec(m, t + 0x34 + k * 12));
						if(scale < 1e-6f)
							scale = 1;
						write_vec(m, t + 0x10 + k * 12, turned[k]);
						write_vec(m, t + 0x34 + k * 12, vmul(turned[k], scale));
						axes[k] = turned[k];
					}
					if(m.u32(t) < 4)
						m.write<std::uint32_t>(t, 4);
				}
			}
			else
			{
				target = current;
				m.write<std::uint32_t>(b + base::end_flags, m.u32(b + base::end_flags) | 0x10000);
			}
		}

		// Walls push the target out of them: the actor is a cylinder from the step height up to
		// body_height (docs/formats/dynamics.md). Run before the floor search, so the floor under
		// where the actor ends up is the one it lands on.
		vec wall_normal{};
		const vec asked = move();
		const std::vector<zone_sphere> zones = zone_spheres(m, actor);
		// Climbing over a ledge (end flag 0x400000, procedure 297): the exe leaves out floor contacts
		// and those below the height at +0x1EC (0x43EEC0 -> 0x4377F0), so the ledge's own face
		// doesn't hold the actor back while the climb animation carries it over the edge.
		const bool climbing = (m.u32(b + base::end_flags) & 0x400000) != 0;
		const float climb_height = climbing ? m.f32(b + 0x1EC) : 0.0f;
		if((flags & collides) && !world.empty() && !placed)
		{
			const float radius = body_radius(zones);
			const float wall_bottom = climbing ? std::max(target[2] + climb_up, climb_height) : target[2] + climb_up;
			std::array<float, 2> push{};
			if(wall_bottom < target[2] + body_height)
				push = world.wall_push(target[0], target[1], wall_bottom, target[2] + body_height, radius);
			target[0] += push[0];
			target[1] += push[1];
			// Touching counts from a little further out, so an actor resting against a wall (pushed out
			// to exactly its radius last tick) still reports it.
			std::array<float, 2> touch{};
			if(wall_bottom < target[2] + body_height)
				touch = world.wall_push(target[0], target[1], wall_bottom, target[2] + body_height, radius + wall_touch);
			if(float l = std::sqrt(touch[0] * touch[0] + touch[1] * touch[1]); l > 1e-6f)
				wall_normal = { touch[0] / l, touch[1] / l, 0.0f };
		}
		const bool on_wall = wall_normal[0] != 0 || wall_normal[1] != 0;

		// The floor. The exe gathers contacts with the actor's collision spheres and picks the
		// nearest (0x43EEC0, 0x43F510); here the actor is a small disc at its origin, tested straight down.
		std::uint32_t block = m.u32(b + base::contacts);
		bool has_block = block && m.contains(block, contacts_size);
		bool landed = false;
		world_collision::surface_hit ground{};
		if((flags & collides) && !world.empty() && !climbing)
		{
			bool was_grounded = has_block && (m.u32(block + 4) & on_ground);
			float top = std::max(current[2], target[2]) + climb_up;
			// Following the floor down is for walking; a move up (a jump) leaves it (the exe only
			// refuses moves into a surface).
			float bottom = target[2] - (was_grounded && target[2] <= current[2] ? follow_down : 0.0f);
			// The actor's feet cover a small circle: the highest floor under any of nine points of it.
			std::optional<world_collision::surface_hit> floor;
			for(int k = 0; k < 9; k++)
			{
				float angle = float(k) * 0.785398f;
				float r = k == 8 ? 0.0f : foot_radius;
				if(auto f = world.floor_at(target[0] + r * std::cos(angle), target[1] + r * std::sin(angle), top, bottom); f && (!floor || f->height > floor->height))
					floor = f;
			}
			if(floor)
			{
				target[2] = floor->height;
				ground = *floor;
				landed = true;
			}
		}

		// The water the actor's body is in: the surface of the pond over it between its feet and the
		// top of its body. The exe keeps water out of the floor and wall resolve and makes it a
		// contact of its own (0x43F510, 0x43EEC0); the scripts then test report bit 0x40 and read
		// slot 4 - condition_32, condition_65(function_197(4), 0x4000) - for the water height
		// (function_192(4).z) and its normal (function_190(4)), which macro 36/37 push particles along.
		std::optional<world_collision::surface_hit> water;
		if((flags & collides) && !world.empty())
			water = world.water_at(target[0], target[1], target[2] + body_height, target[2]);

		// The speed the scripts read back (0x436A30); landing drops the vertical part (0x43B337).
		// The exe works it out before the collision step, so being pushed out of a wall isn't speed
		// (the real game's portal release: 0.3 off the wall in one frame, sideways speed -0.07);
		// what went into the wall is taken off instead. Vertically the floor's answer counts.
		vec speed;
		vec along = asked;
		if(on_wall)
			if(float into = along[0] * wall_normal[0] + along[1] * wall_normal[1]; into < 0)
				for(int i = 0; i < 2; i++)
					along[std::size_t(i)] -= wall_normal[std::size_t(i)] * into;
		for(int i = 0; i < 2; i++)
			speed[std::size_t(i)] = along[std::size_t(i)] / dt;
		speed[2] = (target[2] - current[2]) / dt;
		if(landed)
			speed[2] = 0;
		write_vec(m, b + base::speed, speed);
		write_vec(m, t + 4, target);
		write_vec(m, b + base::previous_position, target);
		// 0x431AD0: dynamics also records target position and movement for the camera's tracked
		// points when both reporting flags are enabled. Procedure 444 consumes the last tick's data.
		if(has_block && (m.u32(b + base::end_flags) & 0x4004) == 0x4004)
		{
			write_vec(m, block + 0x114, speed);
			write_vec(m, block + 0x14C, target);
		}
		if(has_block && (flags & collides))
		{
			// The word at +4 is what the scripts read; +0 is this tick's, published at its end
			// (0x43F940). Slot 1 (the ground) and slot 0 (the tick's first contact, the ground
			// being the only one we find) hold the floor's normal, the point under the actor and
			// its game material; nothing touched leaves them clear. Scripts read them through
			// functions 190..197 and conditions 61..64.
			const vec point = { target[0], target[1], target[2] };
			// In the air, a ledge probe (a zone in use whose material has collide flag 0x8: Rayman's
			// is 2 units ahead, 1.9 up, radius 1) touching a grabbable surface (0x4) is the tick's
			// contact instead, with the probe's material as the actor's own (+0x1C). Rayman's macro 3
			// grabs the ledge on it (condition 34 with 0x8, function 193). Seen in the real game with
			// tools/r2live.py: the touch point lies on the ledge's top, 1.0 from the probe's centre.
			// Only report it while descending: a probe can reach the top while Rayman is still rising.
			std::optional<world_collision::segment_hit> probe;
			std::uint32_t probe_material = 0;
			if(!landed && speed[2] < 0.0f && !world.empty())
				for(const zone_sphere& z : zones)
				{
					if(!z.in_use || !(material_flags(m, z.material) & ledge_probe))
						continue;
					vec c = to_world(z.centre);
					for(int i = 0; i < 3; i++)
						c[std::size_t(i)] += target[std::size_t(i)];
					if((probe = world.sphere_touch(c, z.radius, grabbable)))
					{
						probe_material = z.material;
						break;
					}
				}
			const std::uint32_t own = landed && !zones.empty() ? zones.front().material : probe_material;
			for(std::uint32_t slot : { 0u, 1u })
			{
				std::uint32_t entry = block + slots_at + slot * slot_size;
				write_vec(m, entry + slot_normal, landed ? ground.normal : probe ? probe->normal : vec{});
				write_vec(m, entry + slot_point, landed ? point : probe ? probe->point : vec{});
				m.write<std::uint32_t>(entry + slot_own, landed || probe ? own : 0);
				m.write<std::uint32_t>(entry + slot_material, landed ? ground.material : probe ? probe->material : 0);
			}
			// Slot 4 is the water (0x43F940: report bit 0x40 into slots_at + 4 * slot_size), with
			// the surface's normal, the point on it above the actor and its game material; the
			// word's 0x40 bit alongside the ground's. Out of the water both are cleared.
			// Slot 2 is a wall (bit 0x4): the way it pushed the actor out, and where. Rayman's macro 40
			// bounces his push (var87) off it, which ends the portal hop against the tablet's wall.
			std::uint32_t word = (landed || probe ? on_ground : 0) | (water ? in_water : 0) | (on_wall ? against_wall : 0);
			m.write<std::uint32_t>(block, word);
			m.write<std::uint32_t>(block + 4, word);
			std::uint32_t wall = block + slots_at + wall_slot * slot_size;
			write_vec(m, wall + slot_normal, wall_normal);
			write_vec(m, wall + slot_point, on_wall ? point : vec{});
			std::uint32_t entry = block + slots_at + water_slot * slot_size;
			const vec water_point = { target[0], target[1], water ? water->height : 0.0f };
			write_vec(m, entry + slot_normal, water ? water->normal : vec{});
			write_vec(m, entry + slot_point, water ? water_point : vec{});
			m.write<std::uint32_t>(entry + slot_material, water ? water->material : 0);
		}
		// The script speeds last one tick (0x4327D9).
		m.write<std::uint32_t>(b + base::flags, flags & kept_after_tick);
		for(std::uint32_t at = base::imposed; at < base::speed; at += 4)
			m.write<float>(b + at, 0.0f);
	}
}
