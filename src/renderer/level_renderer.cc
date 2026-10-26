#include <renderer/level_renderer.h>

#include <openrayman/gf_texture.h>

#include <glm/gtc/type_ptr.hpp>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdint>

namespace openrayman
{
	namespace
	{
		const char* vertex_shader = R"(
			layout(location = 0) in vec3 a_position;
			layout(location = 1) in vec2 a_uv;
			layout(location = 2) in vec3 a_normal;
			uniform mat4 u_view_projection;
			out vec2 v_uv;
			out float v_light;
			void main()
			{
				// Stand-in lighting until the game's own is decoded: a soft light from above, so
				// shapes are readable.
				vec3 sun = normalize(vec3(0.3, 0.2, 0.9));
				v_light = 0.6 + 0.4 * abs(dot(normalize(a_normal), sun));
				v_uv = a_uv;
				gl_Position = u_view_projection * vec4(a_position, 1.0);
			}
		)";

		// Hard-edged alpha (leaves, ferns) is cut out; soft alpha is blended in a second pass.
		const char* fragment_shader = R"(
			in vec2 v_uv;
			in float v_light;
			out vec4 o_color;
			uniform sampler2D u_texture;
			uniform float u_alpha_cut;
			void main()
			{
				vec4 texel = texture(u_texture, v_uv);
				if(texel.a < u_alpha_cut)
					discard;
				o_color = vec4(texel.rgb * v_light, u_alpha_cut > 0.25 ? 1.0 : texel.a);
			}
		)";

		// Which surfaces the game blends isn't decoded yet, so guess from the texture: one where
		// many pixels are partly transparent is a soft effect rather than a cut-out.
		bool has_soft_alpha(const gf_texture& gf)
		{
			if(gf.channels < 4)
				return false;
			std::size_t soft = 0, pixels = gf.rgba.size() / 4;
			for(std::size_t i = 3; i < gf.rgba.size(); i += 4)
				soft += gf.rgba[i] > 16 && gf.rgba[i] < 240;
			return soft * 10 > pixels;
		}

		struct vertex
		{
			glm::vec3 position;
			glm::vec2 uv;
			glm::vec3 normal;
		};

		GLuint upload_texture(std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba)
		{
			GLuint texture = 0;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
			glGenerateMipmap(GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			// Level UVs run past 0..1 to tile textures across large surfaces.
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			return texture;
		}
	}

	level_renderer::level_renderer(gl::api api)
	{
		m_program = gl::create_program(api, vertex_shader, fragment_shader);
		m_u_view_projection = glGetUniformLocation(m_program, "u_view_projection");
		m_u_alpha_cut = glGetUniformLocation(m_program, "u_alpha_cut");
		glUseProgram(m_program);
		glUniform1i(glGetUniformLocation(m_program, "u_texture"), 0);

		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glBindVertexArray(m_vao);
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, position)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, uv)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, normal)));
		glBindVertexArray(0);

		const std::uint8_t white[4] = { 255, 255, 255, 255 };
		m_white = upload_texture(1, 1, white);
	}

	level_renderer::~level_renderer()
	{
		clear();
		glDeleteTextures(1, &m_white);
		glDeleteBuffers(1, &m_vbo);
		glDeleteVertexArrays(1, &m_vao);
		glDeleteProgram(m_program);
	}

	void level_renderer::clear()
	{
		for(const auto& [path, texture] : m_textures)
		{
			if(texture.id)
				glDeleteTextures(1, &texture.id);
		}
		m_textures.clear();
		m_batches.clear();
	}

	level_renderer::gpu_texture level_renderer::texture_for(const texture_info& info, std::span<const std::unique_ptr<cnt_archive>> archives, statistics& stats)
	{
		std::string path = info.cnt_path();
		if(auto it = m_textures.find(path); it != m_textures.end())
			return it->second;

		gpu_texture texture;
		for(const auto& archive : archives)
		{
			if(const cnt_entry* entry = archive->find(path))
			{
				try
				{
					gf_texture gf = decode_gf(archive->read(*entry));
					texture.id = upload_texture(gf.width, gf.height, gf.rgba.data());
					texture.blended = has_soft_alpha(gf);
				}
				catch(const std::exception& e)
				{
					SDL_Log("Texture %s: %s", path.c_str(), e.what());
				}
				break;
			}
		}
		if(texture.id)
			stats.textures++;
		else
			stats.missing_textures++;
		m_textures[path] = texture;
		return texture;
	}

	level_renderer::statistics level_renderer::load(std::span<const placed_geometry> geometry, std::span<const std::unique_ptr<cnt_archive>> archives)
	{
		clear();
		statistics stats;
		stats.min = glm::vec3(1e30f);
		stats.max = glm::vec3(-1e30f);

		// Triangles grouped by texture. Corners are expanded into separate vertices, because a
		// corner's UV is indexed separately from its position.
		std::map<std::pair<GLuint, bool>, std::vector<vertex>> by_texture;
		for(const placed_geometry& placed : geometry)
		{
			stats.meshes++;
			const geometric_object& object = placed.object;
			for(const triangle_element& element : object.elements)
			{
				gpu_texture texture{ m_white, false };
				if(element.material && element.material->texture)
				{
					texture = texture_for(*element.material->texture, archives, stats);
					if(!texture.id)
						texture = { m_white, false };
				}
				std::vector<vertex>& out = by_texture[{ texture.id, texture.blended }];
				for(std::size_t t = 0; t < element.triangles.size(); t++)
				{
					for(int k = 0; k < 3; k++)
					{
						std::uint16_t index = element.triangles[t][k];
						vec3 p = placed.world.apply(object.vertices[index]);
						vec3 n = index < object.normals.size() ? object.normals[index] : vec3{ 0, 0, 1 };
						vec2 uv = element.uvs[element.uv_triangles[t][k]];
						glm::vec3 position(p[0], p[1], p[2]);
						stats.min = glm::min(stats.min, position);
						stats.max = glm::max(stats.max, position);
						out.push_back({ position, glm::vec2(uv[0], uv[1]), glm::vec3(n[0], n[1], n[2]) });
					}
					stats.triangles++;
				}
			}
		}

		std::vector<vertex> vertices;
		for(const auto& [key, list] : by_texture)
		{
			m_batches.push_back({ key.first, GLint(vertices.size()), GLsizei(list.size()), key.second });
			vertices.insert(vertices.end(), list.begin(), list.end());
		}
		// Solid batches first, blended ones after them.
		std::ranges::stable_partition(m_batches, [](const batch& b) { return !b.blended; });
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(vertex)), vertices.data(), GL_STATIC_DRAW);
		if(vertices.empty())
			stats.min = stats.max = glm::vec3(0);
		return stats;
	}

	void level_renderer::draw(const glm::mat4& view_projection) const
	{
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE); // which faces are double sided isn't decoded yet
		glUseProgram(m_program);
		glUniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
		glBindVertexArray(m_vao);
		glActiveTexture(GL_TEXTURE0);
		bool blending = false;
		glUniform1f(m_u_alpha_cut, 0.5f);
		for(const batch& b : m_batches)
		{
			if(b.blended && !blending)
			{
				// Soft effects: blended over the solid scene, without hiding each other.
				blending = true;
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDepthMask(GL_FALSE);
				glUniform1f(m_u_alpha_cut, 0.01f);
			}
			glBindTexture(GL_TEXTURE_2D, b.texture);
			glDrawArrays(GL_TRIANGLES, b.first, b.count);
		}
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		glBindVertexArray(0);
		glDisable(GL_DEPTH_TEST);
	}
}
