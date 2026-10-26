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
	// One image of an animated texture, and how long it shows.
	struct texture_frame
	{
		texture_info texture;
		float duration = 0; // seconds
	};

	struct visual_material
	{
		memory_location at;
		std::uint32_t flags = 0;
		rgba ambient{}, diffuse{}, specular{};
		std::optional<texture_info> texture;
		// An animated texture: images shown in turn, looping (e.g. the flapping wings of Lums).
		// Empty for a still texture.
		std::vector<texture_frame> frames;
	};

	// Element types inside a geometric object.
	namespace element_type
	{
		constexpr std::uint16_t triangles = 1;
		constexpr std::uint16_t sprites = 3;
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

	// A flat image that always faces the camera (Lums, glows, sparkles), centred on a vertex.
	struct sprite
	{
		std::uint16_t vertex = 0;
		vec2 half_size{ 1, 1 }; // half the width and height, in world units
		std::optional<visual_material> material;
	};

	enum class look_at : std::uint32_t
	{
		none = 0,
		upright = 1, // turns about its own Z axis only (mushrooms, flowers)
		facing = 2,  // faces the camera fully (flames, sparks, chain links)
	};

	// A mesh: vertices plus elements. See docs/formats/geometry.md.
	struct geometric_object
	{
		memory_location at;
		std::vector<vec3> vertices;
		std::vector<vec3> normals;
		std::vector<triangle_element> elements;
		std::vector<sprite> sprites;
		std::vector<std::uint16_t> skipped_types; // elements of types not decoded yet
		// Cards that turn towards the camera. They lie flat in their own YZ plane and face +X.
		look_at look = look_at::none;
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

		// The transform's combined rotation and scale matrix, then its position.
		static affine from(const transform& t);
		affine operator*(const affine& other) const;
		vec3 apply(const vec3& v) const;
		// How much the matrix scales sizes, on average over the three axes: sprites are sized by it.
		float uniform_scale() const;
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
