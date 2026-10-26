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
			glm::vec3 min{ 0 }, max{ 0 }; // bounds of all vertices
		};

		// Replaces what's on the GPU with this level's geometry. Textures are looked up in `archives`.
		statistics load(std::span<const placed_geometry> geometry, std::span<const std::unique_ptr<cnt_archive>> archives);

		void draw(const glm::mat4& view_projection) const;

	private:
		struct batch
		{
			GLuint texture = 0;
			GLint first = 0;
			GLsizei count = 0;
			bool blended = false;
		};

		struct gpu_texture
		{
			GLuint id = 0;
			bool blended = false; // soft alpha (mist, light shafts): drawn blended, after the rest
		};

		void clear();
		gpu_texture texture_for(const texture_info& info, std::span<const std::unique_ptr<cnt_archive>> archives, statistics& stats);

		GLuint m_program = 0;
		GLint m_u_view_projection = -1;
		GLint m_u_alpha_cut = -1;
		GLuint m_vao = 0, m_vbo = 0;
		GLuint m_white = 0; // for untextured materials
		std::map<std::string, gpu_texture> m_textures; // by CNT path, id 0 if missing
		std::vector<batch> m_batches;
	};
}
