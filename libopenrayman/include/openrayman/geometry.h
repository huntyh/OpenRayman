// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/level_header.h>
#include <openrayman/memory_image.h>
#include <openrayman/scene.h>
#include <openrayman/texture_table.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace openrayman
{
	using vec2 = std::array<float, 2>;
	using vec3 = std::array<float, 3>;
	using rgba = std::array<float, 4>;

	// How a surface looks. Colours are as stored; how the engine combines them isn't confirmed yet.
	struct visual_material
	{
		memory_location at;
		std::uint32_t flags = 0;
		rgba ambient{}, diffuse{}, specular{};
		std::optional<texture_info> texture;
	};

	// Element types inside a geometric object. Only indexed triangles are decoded so far.
	namespace element_type
	{
		constexpr std::uint16_t triangles = 1;
	}

	// A set of triangles sharing one material. Triangle corners index the object's vertices, and
	// separately its own UV list, so one vertex can have different UVs on different triangles.
	struct triangle_element
	{
		std::optional<visual_material> material;
		std::vector<std::array<std::uint16_t, 3>> triangles;
		std::vector<std::array<std::uint16_t, 3>> uv_triangles;
		std::vector<vec3> face_normals;
		std::vector<vec2> uvs;
	};

	// A mesh: vertices plus elements. See docs/formats/geometry.md.
	struct geometric_object
	{
		memory_location at;
		std::vector<vec3> vertices;
		std::vector<vec3> normals;
		std::vector<triangle_element> elements;
		std::vector<std::uint16_t> skipped_types; // elements of types not decoded yet
	};

	// Reads a geometric object. Throws format_error on out-of-range indices or truncated data.
	geometric_object read_geometric_object(const memory_image& memory, const memory_location& at);

	// For the data of an "instantiated physical object" super object (type 0x20, static level
	// geometry): the geometric object of its most detailed level of detail, if it has one.
	std::optional<memory_location> ipo_geometry(const memory_image& memory, const memory_location& ipo);

	// The same for a physical object (what IPOs and actors' object lists point to).
	std::optional<memory_location> physical_object_geometry(const memory_image& memory, const memory_location& physical);

	// Reads a transform (0x58 bytes) from memory.
	transform read_transform(const memory_image& memory, const memory_location& at);

	// A 3x4 affine matrix: rotation and scale in the first three columns, translation in the last.
	struct affine
	{
		std::array<std::array<float, 4>, 3> m{ { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } } };

		// rotation * scale, then the position. The order is an assumption: every static object
		// seen so far has an identity transform, so it isn't confirmed yet.
		static affine from(const transform& t);
		affine operator*(const affine& other) const;
		vec3 apply(const vec3& v) const;
		bool operator==(const affine&) const = default;
	};

	// A mesh of the level's static geometry, placed in the world.
	struct placed_geometry
	{
		std::size_t node = 0; // the super object, in the scene
		affine world;
		geometric_object object;
	};

	// All static geometry: every instantiated physical object in the sector tree, with the
	// transforms of its super object and all its parents applied. Throws format_error.
	std::vector<placed_geometry> static_geometry(const memory_image& memory, const scene& tree);
}
