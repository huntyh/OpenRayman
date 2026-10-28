// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/collision.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <cmath>
#include <format>
#include <span>

namespace openrayman
{
	namespace
	{
		// Physical object +4 -> collide set; its four slots hold geometric objects (zones), and the
		// one the level collides with is the last. Read from the data across all 70 levels and
		// matched with the exe's collision tests (docs/formats/collision.md).
		constexpr std::uint32_t physical_collide_set = 0x04;
		constexpr std::uint32_t collide_set_geometry = 0x0C;
		constexpr std::uint32_t game_material_collide = 0x0C;
		constexpr std::uint32_t collide_material_flags = 0x02;
		constexpr std::uint16_t element_triangles = 1;

		binary_reader reader_at(const memory_location& at, std::size_t size)
		{
			const auto& data = at.block->data;
			if(std::size_t(at.offset) + size > data.size())
				throw format_error(std::format("structure at {} +0x{:X} runs past its block", at.block->id.to_string(), at.offset));
			return binary_reader(std::span(data).subspan(at.offset, size));
		}

		vec3 read_vec3(binary_reader& r)
		{
			float x = r.f32();
			float y = r.f32();
			return { x, y, r.f32() };
		}

		void add_triangles(const memory_image& memory, const memory_location& geometry, const affine& world, std::size_t node,
			std::vector<collision_triangle>& out)
		{
			binary_reader counts = reader_at(geometry + 0x2C, 4);
			std::uint16_t vertex_count = counts.u16();
			std::uint16_t element_count = counts.u16();
			auto vertex_list = memory.follow(geometry);
			auto type_list = memory.follow(geometry + 0x10);
			auto element_list = memory.follow(geometry + 0x14);
			if(!vertex_list || !type_list || !element_list || element_count == 0)
				return;

			std::vector<vec3> vertices;
			binary_reader v = reader_at(*vertex_list, vertex_count * 12u);
			for(std::uint16_t i = 0; i < vertex_count; i++)
				vertices.push_back(world.apply(read_vec3(v)));

			binary_reader types = reader_at(*type_list, element_count * 2u);
			for(std::uint16_t e = 0; e < element_count; e++)
			{
				std::uint16_t type = types.u16();
				auto element = memory.follow(*element_list + e * 4u);
				if(type != element_triangles || !element)
					continue;

				collision_triangle base;
				base.node = node;
				if(auto material = memory.follow(*element))
				{
					if(auto collide = memory.follow(*material + game_material_collide))
					{
						base.has_material = true;
						base.flags = reader_at(*collide + collide_material_flags, 2).u16();
					}
				}

				std::uint16_t count = reader_at(*element + 4, 2).u16();
				auto index_list = memory.follow(*element + 0x08);
				auto normal_list = memory.follow(*element + 0x10);
				if(!index_list || count == 0)
					continue;
				binary_reader indices = reader_at(*index_list, count * 6u);
				std::optional<binary_reader> normals;
				if(normal_list)
					normals = reader_at(*normal_list, count * 12u);
				for(std::uint16_t t = 0; t < count; t++)
				{
					collision_triangle tri = base;
					for(auto& corner : tri.corners)
					{
						std::uint16_t index = indices.u16();
						if(index >= vertices.size())
							throw format_error(std::format("collision triangle uses vertex {} of {}", index, vertices.size()));
						corner = vertices[index];
					}
					// The normal from the corners, which also keeps it right under mirroring scales;
					// the stored one only decides which side it faces.
					const auto& [a, b, c] = tri.corners;
					vec3 u{ b[0] - a[0], b[1] - a[1], b[2] - a[2] }, w{ c[0] - a[0], c[1] - a[1], c[2] - a[2] };
					vec3 n{ u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
					if(normals)
					{
						vec3 stored = read_vec3(*normals);
						vec3 origin = world.apply({ 0, 0, 0 }), tip = world.apply(stored);
						float facing = n[0] * (tip[0] - origin[0]) + n[1] * (tip[1] - origin[1]) + n[2] * (tip[2] - origin[2]);
						if(facing < 0)
							n = { -n[0], -n[1], -n[2] };
					}
					float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
					if(length == 0)
						continue; // degenerate: nothing to collide with
					tri.normal = { n[0] / length, n[1] / length, n[2] / length };
					out.push_back(tri);
				}
			}
		}
	}

	std::optional<memory_location> physical_object_collision(const memory_image& memory, const memory_location& physical)
	{
		auto set = memory.follow(physical + physical_collide_set);
		return set ? memory.follow(*set + collide_set_geometry) : std::nullopt;
	}

	std::vector<collision_triangle> static_collision(const memory_image& memory, const scene& tree)
	{
		std::vector<collision_triangle> result;
		auto visit = [&](auto& self, std::size_t index, const affine& parent) -> void {
			const scene_node& node = tree.node(index);
			affine world = node.matrix ? parent * affine::from(read_transform(memory, *node.matrix)) : parent;
			if(node.type == super_object_type::ipo && node.data)
			{
				if(auto physical = memory.follow(*node.data))
					if(auto geometry = physical_object_collision(memory, *physical))
						add_triangles(memory, *geometry, world, index, result);
			}
			for(std::size_t child : node.children)
				self(self, child, world);
		};
		visit(visit, tree.father_sector(), affine{});
		return result;
	}
}
