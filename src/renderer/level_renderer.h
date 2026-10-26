#pragma once

#include <renderer/gl.h>

#include <openrayman/cnt_archive.h>
#include <openrayman/geometry.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace openrayman
{
	// Draws a level's static geometry. Everything is uploaded once per level, grouped into one
	// draw call per texture.
	class level_renderer
	{
	public:
		explicit level_renderer(gl::api api);
		~level_renderer();
		level_renderer(const level_renderer&) = delete;
		level_renderer& operator=(const level_renderer&) = delete;

		struct statistics
		{
			std::size_t meshes = 0;
			std::size_t triangles = 0;
			std::size_t textures = 0;
			std::size_t missing_textures = 0;
			std::size_t sprites = 0;
			glm::vec3 min{ 0 }, max{ 0 }; // bounds of all vertices
		};

		// Replaces what's on the GPU with this level's geometry. Textures are looked up in `archives`.
		statistics load(std::span<const placed_geometry> geometry, std::span<const std::unique_ptr<cnt_archive>> archives);

		// Sprites turn to face the camera, so this needs the view on its own.
		void draw(const glm::mat4& view, const glm::mat4& projection) const;

	private:
		// Drawn in this order: solid surfaces first, then see-through ones over them.
		enum class blend
		{
			none,     // opaque, hard-edged alpha cut out
			alpha,    // soft alpha (mist, clouds)
			additive, // glows and light
		};

		struct batch
		{
			GLuint texture = 0;
			GLint first = 0;
			GLsizei count = 0;
			blend mode = blend::none;
		};

		struct gpu_texture
		{
			GLuint id = 0;
			blend mode = blend::none;
			bool mirror_u = false, mirror_v = false;
		};

		struct sprite_instance
		{
			glm::vec3 center;
			glm::vec2 half_size;
			GLuint texture = 0;
			blend mode = blend::none;
			glm::vec2 uv_extent{ 1, 1 }; // 2 along a mirrored axis, so the whole image shows
		};

		void clear();
		void set_blend(blend mode) const;
		gpu_texture texture_for(const texture_info& info, std::span<const std::unique_ptr<cnt_archive>> archives, statistics& stats);

		GLuint m_program = 0;
		GLint m_u_view_projection = -1;
		GLint m_u_alpha_cut = -1;
		GLuint m_vao = 0, m_vbo = 0;
		GLuint m_sprite_vao = 0, m_sprite_vbo = 0; // rebuilt every frame
		std::vector<sprite_instance> m_sprites;     // sorted by blend mode, then texture
		GLuint m_white = 0; // for untextured materials
		std::map<std::string, gpu_texture> m_textures; // by CNT path, id 0 if missing
		std::vector<batch> m_batches;
	};
}
