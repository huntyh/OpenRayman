// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/geometry.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <format>

namespace openrayman
{
	// Layouts worked out by following pointers from the sector tree, and checked by the counts
	// and array sizes agreeing (see docs/formats/geometry.md).
	namespace geometry_field
	{
		constexpr std::uint32_t vertices = 0x00;
		constexpr std::uint32_t normals = 0x04;
		constexpr std::uint32_t element_types = 0x10;
		constexpr std::uint32_t elements = 0x14;
		constexpr std::uint32_t vertex_count = 0x2C; // u16, then u16 element count
	}

	namespace element_field
	{
		constexpr std::uint32_t material = 0x00;
		constexpr std::uint32_t triangle_count = 0x04; // u16, then u16 UV count
		constexpr std::uint32_t triangles = 0x08;
		constexpr std::uint32_t uv_triangles = 0x0C;
		constexpr std::uint32_t face_normals = 0x10;
		constexpr std::uint32_t uvs = 0x14;
	}

	namespace material_field
	{
		constexpr std::uint32_t flags = 0x00;
		constexpr std::uint32_t ambient = 0x04;
		constexpr std::uint32_t diffuse = 0x14;
		constexpr std::uint32_t specular = 0x24;
		constexpr std::uint32_t texture = 0x48;
	}

	static std::string describe(const memory_location& at)
	{
		return std::format("{} +0x{:X}", at.block->id.to_string(), at.offset);
	}

	// A reader over `size` bytes of loaded data at a location.
	static binary_reader reader_at(const memory_location& at, std::size_t size)
	{
		const auto& data = at.block->data;
		if(std::size_t(at.offset) + size > data.size())
			throw format_error("data at " + describe(at) + " runs past the loaded block");
		return binary_reader(std::span(data).subspan(at.offset, size));
	}

	static memory_location require(const std::optional<memory_location>& at, const memory_location& from, const char* what)
	{
		if(!at)
			throw format_error(std::string("missing ") + what + " at " + describe(from));
		return *at;
	}

	static vec3 read_vec3(binary_reader& r)
	{
		float x = r.f32(), y = r.f32();
		return { x, y, r.f32() };
	}

	static rgba read_rgba(binary_reader& r)
	{
		float red = r.f32(), green = r.f32(), blue = r.f32();
		return { red, green, blue, r.f32() };
	}

	transform read_transform(const memory_image&, const memory_location& at)
	{
		binary_reader r = reader_at(at, 0x58);
		transform t;
		t.type = r.u32();
		for(float& f : t.position)
			f = r.f32();
		for(float& f : t.rotation)
			f = r.f32();
		for(float& f : t.scale)
			f = r.f32();
		return t;
	}

	static visual_material read_visual_material(const memory_image& memory, const memory_location& at)
	{
		visual_material m;
		m.at = at;
		binary_reader r = reader_at(at, material_field::texture + 4);
		m.flags = r.u32();
		r.seek(material_field::ambient);
		m.ambient = read_rgba(r);
		r.seek(material_field::diffuse);
		m.diffuse = read_rgba(r);
		r.seek(material_field::specular);
		m.specular = read_rgba(r);
		if(auto texture = memory.follow(at + material_field::texture))
			m.texture = read_texture_info(memory, *texture);
		return m;
	}

	static triangle_element read_triangles(const memory_image& memory, const memory_location& at, std::size_t vertex_count)
	{
		triangle_element e;
		binary_reader counts = reader_at(at + element_field::triangle_count, 4);
		std::uint16_t triangles = counts.u16();
		std::uint16_t uvs = counts.u16();

		// Elements point at a "game material", whose first field is the visual material.
		if(auto game_material = memory.follow(at + element_field::material))
		{
			if(auto visual = memory.follow(*game_material))
				e.material = read_visual_material(memory, *visual);
		}

		auto index_list = [&](std::uint32_t field, std::size_t limit, const char* what) {
			std::vector<std::array<std::uint16_t, 3>> list;
			if(triangles == 0)
				return list;
			memory_location data = require(memory.follow(at + field), at, what);
			binary_reader r = reader_at(data, triangles * 6u);
			for(std::uint16_t i = 0; i < triangles; i++)
			{
				std::array<std::uint16_t, 3> t{ r.u16(), r.u16(), r.u16() };
				for(std::uint16_t index : t)
				{
					if(index >= limit)
						throw format_error(std::format("{} at {} uses index {} of {}", what, describe(data), index, limit));
				}
				list.push_back(t);
			}
			return list;
		};
		e.triangles = index_list(element_field::triangles, vertex_count, "triangle");
		e.uv_triangles = index_list(element_field::uv_triangles, uvs, "UV triangle");

		if(triangles > 0)
		{
			if(auto normals = memory.follow(at + element_field::face_normals))
			{
				binary_reader r = reader_at(*normals, triangles * 12u);
				for(std::uint16_t i = 0; i < triangles; i++)
					e.face_normals.push_back(read_vec3(r));
			}
		}
		if(uvs > 0)
		{
			binary_reader r = reader_at(require(memory.follow(at + element_field::uvs), at, "UV list"), uvs * 8u);
			for(std::uint16_t i = 0; i < uvs; i++)
			{
				float u = r.f32();
				e.uvs.push_back({ u, r.f32() });
			}
		}
		return e;
	}

	// Sprite element: a list of sprites, each 0x30 bytes (info, a size, more fields not used yet, the
	// centre vertex). The info holds the half size actually drawn and, two steps away, the material.
	static void read_sprites(const memory_image& memory, const memory_location& at, std::size_t vertex_count, std::vector<sprite>& out)
	{
		constexpr std::uint32_t sprite_size = 0x30;
		std::uint16_t count = reader_at(at + 4, 2).u16();
		if(count == 0)
			return;
		memory_location list = require(memory.follow(at), at, "sprite list");
		reader_at(list, std::size_t(count) * sprite_size); // bounds check
		for(std::uint16_t i = 0; i < count; i++)
		{
			memory_location entry = list + i * sprite_size;
			sprite s;
			s.vertex = reader_at(entry + 0x28, 2).u16();
			if(s.vertex >= vertex_count)
				throw format_error(std::format("sprite at {} is centred on vertex {} of {}", describe(entry), s.vertex, vertex_count));
			if(auto info = memory.follow(entry))
			{
				if(auto scale = memory.follow(*info + 0x0C))
				{
					binary_reader r = reader_at(*scale, 8);
					float x = r.f32();
					s.half_size = { x, r.f32() };
				}
				if(auto material_pointer = memory.follow(*info + 0x14))
					if(auto game_material = memory.follow(*material_pointer))
						if(auto visual = memory.follow(*game_material))
							s.material = read_visual_material(memory, *visual);
			}
			out.push_back(s);
		}
	}

	geometric_object read_geometric_object(const memory_image& memory, const memory_location& at)
	{
		geometric_object object;
		object.at = at;
		binary_reader counts = reader_at(at + geometry_field::vertex_count, 4);
		std::uint16_t vertex_count = counts.u16();
		std::uint16_t element_count = counts.u16();

		if(vertex_count > 0)
		{
			binary_reader v = reader_at(require(memory.follow(at + geometry_field::vertices), at, "vertex list"), vertex_count * 12u);
			for(std::uint16_t i = 0; i < vertex_count; i++)
				object.vertices.push_back(read_vec3(v));
			if(auto normals = memory.follow(at + geometry_field::normals))
			{
				binary_reader n = reader_at(*normals, vertex_count * 12u);
				for(std::uint16_t i = 0; i < vertex_count; i++)
					object.normals.push_back(read_vec3(n));
			}
		}

		if(element_count > 0)
		{
			binary_reader types = reader_at(require(memory.follow(at + geometry_field::element_types), at, "element type list"), element_count * 2u);
			memory_location elements = require(memory.follow(at + geometry_field::elements), at, "element list");
			for(std::uint16_t i = 0; i < element_count; i++)
			{
				std::uint16_t type = types.u16();
				memory_location slot = elements + i * 4u;
				if(type == element_type::triangles)
					object.elements.push_back(read_triangles(memory, require(memory.follow(slot), slot, "element"), vertex_count));
				else if(type == element_type::sprites)
					read_sprites(memory, require(memory.follow(slot), slot, "element"), vertex_count, object.sprites);
				else
					object.skipped_types.push_back(type);
			}
		}
		return object;
	}

	affine affine::from(const transform& t)
	{
		affine a;
		for(int r = 0; r < 3; r++)
		{
			for(int c = 0; c < 3; c++)
			{
				float sum = 0;
				for(int k = 0; k < 3; k++)
					sum += t.rotation[r * 3 + k] * t.scale[k * 3 + c];
				a.m[r][c] = sum;
			}
			a.m[r][3] = t.position[r];
		}
		return a;
	}

	affine affine::operator*(const affine& other) const
	{
		affine a;
		for(int r = 0; r < 3; r++)
		{
			for(int c = 0; c < 4; c++)
			{
				float sum = c == 3 ? m[r][3] : 0;
				for(int k = 0; k < 3; k++)
					sum += m[r][k] * other.m[k][c];
				a.m[r][c] = sum;
			}
		}
		return a;
	}

	vec3 affine::apply(const vec3& v) const
	{
		vec3 out;
		for(int r = 0; r < 3; r++)
			out[r] = m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2] + m[r][3];
		return out;
	}

	std::vector<placed_geometry> static_geometry(const memory_image& memory, const scene& tree)
	{
		std::vector<placed_geometry> result;
		auto visit = [&](auto& self, std::size_t index, const affine& parent) -> void {
			const scene_node& node = tree.node(index);
			affine world = node.matrix ? parent * affine::from(read_transform(memory, *node.matrix)) : parent;
			if(node.type == super_object_type::ipo && node.data)
			{
				if(auto at = ipo_geometry(memory, *node.data))
					result.push_back({ index, world, read_geometric_object(memory, *at) });
			}
			for(std::size_t child : node.children)
				self(self, child, world);
		};
		visit(visit, tree.father_sector(), affine{});
		return result;
	}

	std::optional<memory_location> ipo_geometry(const memory_image& memory, const memory_location& ipo)
	{
		// IPO -> physical object -> visual set -> list of levels of detail.
		auto physical = memory.follow(ipo);
		return physical ? physical_object_geometry(memory, *physical) : std::nullopt;
	}

	std::optional<memory_location> physical_object_geometry(const memory_image& memory, const memory_location& physical)
	{
		auto visual_set = memory.follow(physical);
		if(!visual_set)
			return std::nullopt;
		binary_reader header = reader_at(*visual_set, 0x10);
		std::uint32_t type = header.u32();
		std::uint32_t lods = header.u32();
		if(type != 0 || lods == 0)
			return std::nullopt;
		auto list = memory.follow(*visual_set + 0x0C);
		return list ? memory.follow(*list) : std::nullopt;
	}
}
