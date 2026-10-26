// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/light.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace openrayman
{
	namespace
	{
		namespace sector_field
		{
			constexpr std::uint32_t first_light = 0x0C, light_count = 0x14;
			constexpr std::uint32_t min = 0x60, max = 0x6C;
			constexpr std::uint32_t is_virtual = 0x7C; // u8
			constexpr std::uint32_t size = 0x80;
		}

		namespace light_field
		{
			constexpr std::uint32_t on = 0x00, type = 0x08;
			constexpr std::uint32_t far = 0x0C, near = 0x10;
			constexpr std::uint32_t placement = 0x24; // a transform
			constexpr std::uint32_t color = 0x94;
			constexpr std::uint32_t send = 0xA4, targets = 0xA5, painting = 0xA6, channels = 0xA7; // u8 each
			constexpr std::uint32_t fog_near_blend = 0x18, fog_far_blend = 0xE8; // out of 255
			constexpr std::uint32_t background = 0xF0;
			constexpr std::uint32_t size = 0x160;
		}

		// A sector's light list element: the light, then the next element.
		constexpr std::uint32_t element_next = 4;

		binary_reader reader_at(const memory_location& at, std::size_t size)
		{
			const auto& data = at.block->data;
			if(std::size_t(at.offset) + size > data.size())
				throw format_error(std::format("structure at {} +0x{:X} runs past its block", at.block->id.to_string(), at.offset));
			return binary_reader(std::span(data).subspan(at.offset, size));
		}

		vec3 read_vec3(binary_reader& r)
		{
			float x = r.f32(), y = r.f32();
			return { x, y, r.f32() };
		}

		std::array<float, 4> read_color(binary_reader& r)
		{
			float red = r.f32(), green = r.f32(), blue = r.f32();
			return { red, green, blue, r.f32() };
		}

		light read_light(const memory_image& memory, const memory_location& at)
		{
			binary_reader r = reader_at(at, light_field::size);
			light l;
			l.at = at;
			l.on = r.u8() != 0;
			r.seek(light_field::type);
			std::uint32_t type = r.u32();
			switch(type)
			{
			case 1: case 2: case 4: case 5: case 6:
				l.type = light_type(type);
				break;
			default:
				throw format_error(std::format("light at {} +0x{:X} has unknown type {}", at.block->id.to_string(), at.offset, type));
			}
			l.far = r.f32();
			l.near = r.f32();
			l.placement = affine::from(read_transform(memory, at + light_field::placement));
			r.seek(light_field::color);
			l.color = read_color(r);
			r.seek(light_field::send);
			l.on = l.on && r.u8() != 0;
			l.targets = r.u8();
			l.painting = r.u8() != 0;
			std::uint8_t channels = r.u8();
			if(channels > 2)
				throw format_error(std::format("light at {} +0x{:X} has unknown channels {}", at.block->id.to_string(), at.offset, channels));
			l.channels = light::affects(channels);
			if(l.type == light_type::fog)
			{
				r.seek(light_field::fog_near_blend);
				l.fog_near_blend = r.f32() / 255.0f;
				r.seek(light_field::fog_far_blend);
				l.fog_far_blend = r.f32() / 255.0f;
				r.seek(light_field::background);
				l.background = read_color(r);
			}
			return l;
		}

		float dot(const vec3& a, const vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
	}

	vec3 light::direction() const
	{
		// The matrix's Z axis: its third column, as affine holds it.
		vec3 d{ placement.m[0][2], placement.m[1][2], placement.m[2][2] };
		float length = std::sqrt(dot(d, d));
		if(length < 1e-6f)
			return { 0, 0, -1 };
		return { d[0] / length, d[1] / length, d[2] / length };
	}

	bool sector::contains(const vec3& p) const
	{
		for(int axis = 0; axis < 3; axis++)
		{
			if(p[axis] < min[axis] || p[axis] > max[axis])
				return false;
		}
		return true;
	}

	std::optional<std::size_t> level_lights::sector_of(const scene& tree, std::size_t node) const
	{
		for(std::optional<std::size_t> n = node; n; n = tree.node(*n).parent)
		{
			for(std::size_t s = 0; s < sectors.size(); s++)
			{
				if(sectors[s].node == *n)
					return s;
			}
		}
		return std::nullopt;
	}

	std::optional<std::size_t> level_lights::sector_at(const vec3& point) const
	{
		// Inside: the smallest box. Outside all of them (a gap between two): the nearest.
		std::optional<std::size_t> best;
		float best_volume = 0, best_distance = 0;
		for(std::size_t s = 0; s < sectors.size(); s++)
		{
			const sector& c = sectors[s];
			if(c.is_virtual || c.min[0] > c.max[0])
				continue;
			float distance = 0, volume = 1;
			for(int axis = 0; axis < 3; axis++)
			{
				float outside = std::max({ c.min[axis] - point[axis], point[axis] - c.max[axis], 0.0f });
				distance += outside * outside;
				volume *= c.max[axis] - c.min[axis];
			}
			if(!best || distance < best_distance || (distance == best_distance && volume < best_volume))
				best = s, best_distance = distance, best_volume = volume;
		}
		return best;
	}

	const light* level_lights::fog(std::size_t s) const
	{
		for(std::size_t i : sectors.at(s).lights)
		{
			if(lights[i].type == light_type::fog && lights[i].on)
				return &lights[i];
		}
		return nullptr;
	}

	level_lights read_lights(const memory_image& memory, const scene& tree)
	{
		level_lights out;
		std::map<std::pair<const sna_block*, std::uint32_t>, std::size_t> index;
		for(std::size_t n = 0; n < tree.nodes().size(); n++)
		{
			const scene_node& node = tree.node(n);
			if(node.type != super_object_type::sector || !node.data)
				continue;
			const memory_location& at = *node.data;
			binary_reader r = reader_at(at, sector_field::size);
			sector s;
			s.node = n;
			r.seek(sector_field::light_count);
			std::uint32_t count = r.u32();
			r.seek(sector_field::min);
			s.min = read_vec3(r);
			s.max = read_vec3(r);
			r.seek(sector_field::is_virtual);
			s.is_virtual = r.u8() != 0;
			std::optional<memory_location> element = memory.follow(at + sector_field::first_light);
			for(std::uint32_t i = 0; i < count; i++)
			{
				if(!element)
					throw format_error(std::format("sector at {} +0x{:X} lists {} lights but has only {}", at.block->id.to_string(), at.offset, count, i));
				std::optional<memory_location> l = memory.follow(*element);
				if(!l)
					throw format_error("a sector light list element points nowhere");
				auto [it, added] = index.try_emplace({ l->block, l->offset }, out.lights.size());
				if(added)
					out.lights.push_back(read_light(memory, *l));
				s.lights.push_back(it->second);
				element = memory.follow(*element + element_next);
			}
			if(element)
				throw format_error(std::format("sector at {} +0x{:X} has more lights than its count {}", at.block->id.to_string(), at.offset, count));
			out.sectors.push_back(std::move(s));
		}
		return out;
	}

	rgba light_at(std::span<const light* const> lights, std::uint8_t target, const vec3& position, const vec3& normal)
	{
		rgba sum{};
		for(const light* l : lights)
		{
			if(!l->on || !(l->targets & target))
				continue;
			float strength = 0;
			switch(l->type)
			{
			case light_type::ambient:
				strength = 1;
				break;
			case light_type::parallel:
			case light_type::parallel_other:
			{
				vec3 d = l->direction();
				strength = std::max(0.0f, -dot(normal, d));
				break;
			}
			case light_type::spherical:
			{
				vec3 p = l->position();
				vec3 to{ p[0] - position[0], p[1] - position[1], p[2] - position[2] };
				float distance = std::sqrt(dot(to, to));
				if(distance >= l->far)
					break;
				float falloff = distance <= l->near || l->far <= l->near ? 1.0f : (l->far - distance) / (l->far - l->near);
				float facing = distance > 1e-5f && !l->painting ? std::max(0.0f, dot(normal, to) / distance) : 1.0f;
				strength = falloff * facing;
				break;
			}
			case light_type::fog:
				break;
			}
			if(l->channels != light::affects::alpha)
			{
				for(int c = 0; c < 3; c++)
					sum[c] += l->color[c] * strength;
			}
			if(l->channels != light::affects::color)
				sum[3] += l->color[3] * strength;
		}
		return sum;
	}
}
