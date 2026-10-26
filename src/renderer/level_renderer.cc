#include <renderer/level_renderer.h>

#include <openrayman/gf_texture.h>

#include <glm/gtc/type_ptr.hpp>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <tuple>

namespace openrayman
{
	namespace
	{
		const char* vertex_shader = R"(
			layout(location = 0) in vec3 a_position;
			layout(location = 1) in vec2 a_uv;
			layout(location = 2) in vec3 a_normal;
			layout(location = 3) in vec4 a_uv_bounds;
			uniform mat4 u_view_projection;
			uniform mat4 u_model;
			out vec2 v_uv;
			flat out vec4 v_uv_bounds;
			out float v_light;
			void main()
			{
				// Stand-in lighting until the game's own is decoded: a soft light from above, so
				// shapes are readable.
				vec3 sun = normalize(vec3(0.3, 0.2, 0.9));
				v_light = 0.6 + 0.4 * abs(dot(normalize(mat3(u_model) * a_normal), sun));
				v_uv = a_uv;
				v_uv_bounds = a_uv_bounds;
				gl_Position = u_view_projection * u_model * vec4(a_position, 1.0);
			}
		)";

		// Hard-edged alpha (leaves, ferns) is cut out; soft alpha is blended in a second pass.
		const char* fragment_shader = R"(
			in vec2 v_uv;
			in float v_light;
			flat in vec4 v_uv_bounds;
			out vec4 o_color;
			uniform sampler2D u_texture;
			uniform float u_alpha_cut;
			uniform float u_edge_clamp;
			void main()
			{
				// A triangle that stays within one copy of its texture keeps half a texel inside
				// that copy's edges, so filtering doesn't bring in the opposite edge as a line.
				vec2 uv = v_uv;
				if(u_edge_clamp > 0.5)
				{
					vec2 half_texel = 0.5 / vec2(textureSize(u_texture, 0));
					uv = clamp(uv, v_uv_bounds.xy + half_texel, v_uv_bounds.zw - half_texel);
				}
				vec4 texel = texture(u_texture, uv);
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
			glm::vec4 uv_bounds{ -1e6f, -1e6f, 1e6f, 1e6f }; // min u, v, max u, v: see the fragment shader
		};

		// The copy of the texture a triangle's UVs stay within, per axis, or no bounds if they cross
		// from one copy into the next (the texture tiles across the triangle).
		glm::vec4 uv_bounds(const glm::vec2 (&uv)[3])
		{
			glm::vec4 bounds{ -1e6f, -1e6f, 1e6f, 1e6f };
			for(int axis = 0; axis < 2; axis++)
			{
				float lo = std::min({ uv[0][axis], uv[1][axis], uv[2][axis] });
				float hi = std::max({ uv[0][axis], uv[1][axis], uv[2][axis] });
				float tile = std::floor(lo + 1e-4f);
				if(hi <= tile + 1 + 1e-4f)
				{
					bounds[axis] = tile;
					bounds[axis + 2] = tile + 1;
				}
			}
			return bounds;
		}

		glm::mat4 to_mat4(const affine& a)
		{
			glm::mat4 m(1.0f);
			for(int r = 0; r < 3; r++)
			{
				for(int c = 0; c < 4; c++)
					m[c][r] = a.m[r][c];
			}
			return m;
		}

		// Turns a card (flat in its own YZ plane, facing +X) towards the camera, keeping its
		// position and the size along each of its axes.
		glm::mat4 face_camera(const glm::mat4& model, look_at look, const glm::mat4& camera)
		{
			if(look == look_at::none)
				return model;
			glm::vec3 position(model[3]);
			glm::vec3 to_camera = glm::vec3(camera[3]) - position;
			glm::vec3 x, z;
			if(look == look_at::upright)
			{
				z = glm::normalize(glm::vec3(model[2]));
				x = to_camera - glm::dot(to_camera, z) * z;
			}
			else
			{
				x = to_camera;
				glm::vec3 up(camera[1]);
				z = up - glm::dot(up, glm::normalize(x)) * glm::normalize(x);
			}
			if(glm::length(x) < 1e-5f || glm::length(z) < 1e-5f)
				return model;
			x = glm::normalize(x);
			z = glm::normalize(z);
			glm::vec3 y = glm::cross(z, x);
			glm::mat4 out(1.0f);
			out[0] = glm::vec4(x * glm::length(glm::vec3(model[0])), 0);
			out[1] = glm::vec4(y * glm::length(glm::vec3(model[1])), 0);
			out[2] = glm::vec4(z * glm::length(glm::vec3(model[2])), 0);
			out[3] = model[3];
			return out;
		}

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
		m_u_model = glGetUniformLocation(m_program, "u_model");
		m_u_alpha_cut = glGetUniformLocation(m_program, "u_alpha_cut");
		m_u_edge_clamp = glGetUniformLocation(m_program, "u_edge_clamp");
		glUseProgram(m_program);
		glUniform1i(glGetUniformLocation(m_program, "u_texture"), 0);

		auto vertex_layout = [](GLuint& vao, GLuint& vbo) {
			glGenVertexArrays(1, &vao);
			glGenBuffers(1, &vbo);
			glBindVertexArray(vao);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, position)));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, uv)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, normal)));
			glEnableVertexAttribArray(3);
			glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, uv_bounds)));
			glBindVertexArray(0);
		};
		vertex_layout(m_vao, m_vbo);
		vertex_layout(m_sprite_vao, m_sprite_vbo);

		const std::uint8_t white[4] = { 255, 255, 255, 255 };
		m_white = upload_texture(1, 1, white);
	}

	level_renderer::~level_renderer()
	{
		clear();
		glDeleteTextures(1, &m_white);
		glDeleteBuffers(1, &m_vbo);
		glDeleteVertexArrays(1, &m_vao);
		glDeleteBuffers(1, &m_sprite_vbo);
		glDeleteVertexArrays(1, &m_sprite_vao);
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
		m_sprites.clear();
		m_meshes.clear();
		m_parts.clear();
		m_level_cards.clear();
		m_animations.clear();
		m_animation_by_material.clear();
	}

	void level_renderer::set_blend(blend mode) const
	{
		// See-through surfaces go over the solid scene, without hiding each other.
		if(mode == blend::none)
		{
			glDisable(GL_BLEND);
			glDepthMask(GL_TRUE);
			glUniform1f(m_u_alpha_cut, 0.5f);
			return;
		}
		glEnable(GL_BLEND);
		glDepthMask(GL_FALSE);
		glUniform1f(m_u_alpha_cut, 0.01f);
		if(mode == blend::additive)
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		else
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
					if(info.flags & texture_flag::color_key)
					{
						// Black is transparent: these textures have no alpha of their own.
						for(std::size_t i = 0; i < gf.rgba.size(); i += 4)
						{
							if(gf.rgba[i] < 8 && gf.rgba[i + 1] < 8 && gf.rgba[i + 2] < 8)
								gf.rgba[i + 3] = 0;
						}
					}
					texture.id = upload_texture(gf.width, gf.height, gf.rgba.data());
					texture.mirror_u = info.tiling & texture_tiling::mirror_u;
					texture.mirror_v = info.tiling & texture_tiling::mirror_v;
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture.mirror_u ? GL_MIRRORED_REPEAT : GL_REPEAT);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture.mirror_v ? GL_MIRRORED_REPEAT : GL_REPEAT);
					if(info.flags & texture_flag::additive)
						texture.mode = blend::additive;
					else if(has_soft_alpha(gf))
						texture.mode = blend::alpha;
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

	int level_renderer::animation_for(const visual_material& material, std::span<const std::unique_ptr<cnt_archive>> archives, statistics& stats)
	{
		if(material.frames.size() < 2)
			return -1;
		if(auto it = m_animation_by_material.find(material.at.address()); it != m_animation_by_material.end())
			return it->second;
		texture_animation animation;
		float end = 0;
		for(const texture_frame& frame : material.frames)
		{
			GLuint id = texture_for(frame.texture, archives, stats).id;
			animation.textures.push_back(id ? id : m_white);
			end += std::max(frame.duration, 0.0f);
			animation.ends.push_back(end);
		}
		int index = -1;
		if(end > 0) // all-zero durations would never move on
		{
			index = int(m_animations.size());
			m_animations.push_back(std::move(animation));
		}
		m_animation_by_material[material.at.address()] = index;
		return index;
	}

	GLuint level_renderer::texture_at(int animation, GLuint still) const
	{
		if(animation < 0 || std::size_t(animation) >= m_animations.size())
			return still;
		const texture_animation& a = m_animations[std::size_t(animation)];
		float t = float(std::fmod(m_time, double(a.ends.back())));
		auto it = std::ranges::upper_bound(a.ends, t);
		return it == a.ends.end() ? a.textures.back() : a.textures[std::size_t(it - a.ends.begin())];
	}

	level_renderer::statistics level_renderer::load(std::span<const placed_geometry> geometry, std::span<const geometric_object> meshes,
		std::span<const std::unique_ptr<cnt_archive>> archives)
	{
		clear();
		statistics stats;
		stats.min = glm::vec3(1e30f);
		stats.max = glm::vec3(-1e30f);

		// Triangles grouped by texture. Corners are expanded into separate vertices, because a
		// corner's UV is indexed separately from its position.
		using by_texture = std::map<std::tuple<blend, GLuint, int>, std::vector<vertex>>;
		auto add = [&](const geometric_object& object, const affine& world, by_texture& triangles, std::vector<sprite_instance>& sprites) {
			stats.meshes++;
			for(const triangle_element& element : object.elements)
			{
				gpu_texture texture{ m_white, blend::none };
				int animation = -1;
				if(element.material && element.material->texture)
				{
					texture = texture_for(*element.material->texture, archives, stats);
					if(!texture.id)
						texture = { m_white, blend::none };
					animation = animation_for(*element.material, archives, stats);
				}
				std::vector<vertex>& out = triangles[{ texture.mode, texture.id, animation }];
				for(std::size_t t = 0; t < element.triangles.size(); t++)
				{
					// The game's v runs up from the bottom row; our textures are uploaded top row first.
					glm::vec2 uvs[3];
					for(int k = 0; k < 3; k++)
					{
						vec2 uv = element.uvs[element.uv_triangles[t][k]];
						uvs[k] = glm::vec2(uv[0], 1.0f - uv[1]);
					}
					glm::vec4 bounds = uv_bounds(uvs);
					for(int k = 0; k < 3; k++)
					{
						std::uint16_t index = element.triangles[t][k];
						vec3 p = world.apply(object.vertices[index]);
						vec3 n = index < object.normals.size() ? object.normals[index] : vec3{ 0, 0, 1 };
						glm::vec3 position(p[0], p[1], p[2]);
						stats.min = glm::min(stats.min, position);
						stats.max = glm::max(stats.max, position);
						out.push_back({ position, uvs[k], glm::vec3(n[0], n[1], n[2]), bounds });
					}
					stats.triangles++;
				}
			}

			for(const sprite& sp : object.sprites)
			{
				gpu_texture texture{ m_white, blend::alpha };
				int animation = -1;
				if(sp.material && sp.material->texture)
				{
					texture = texture_for(*sp.material->texture, archives, stats);
					if(!texture.id)
						continue;
					animation = animation_for(*sp.material, archives, stats);
				}
				vec3 c = world.apply(object.vertices[sp.vertex]);
				// Sprites scale with the part they belong to (a Lum's blue core is about half its glow).
				float scale = world.uniform_scale();
				sprites.push_back({ glm::vec3(c[0], c[1], c[2]), glm::vec2(sp.half_size[0], sp.half_size[1]) * scale, texture.id, texture.mode,
					glm::vec2(texture.mirror_u ? 2.0f : 1.0f, texture.mirror_v ? 2.0f : 1.0f), animation });
				stats.sprites++;
			}
		};

		// One vertex buffer: the static geometry first, then each actor mesh.
		std::vector<vertex> vertices;
		auto append = [&](const by_texture& triangles, std::vector<batch>& batches) {
			for(const auto& [key, list] : triangles)
			{
				batches.push_back({ std::get<1>(key), GLint(vertices.size()), GLsizei(list.size()), std::get<0>(key), std::get<2>(key) });
				vertices.insert(vertices.end(), list.begin(), list.end());
			}
		};

		by_texture level_triangles;
		std::vector<const placed_geometry*> cards;
		for(const placed_geometry& placed : geometry)
		{
			if(placed.object.look == look_at::none)
				add(placed.object, placed.world, level_triangles, m_sprites);
			else
				cards.push_back(&placed);
		}
		append(level_triangles, m_batches);
		// Meshes placed per frame are in their own space, so they don't count towards the level's bounds.
		glm::vec3 level_min = stats.min, level_max = stats.max;
		auto add_mesh = [&](const geometric_object& object) {
			by_texture triangles;
			mesh& m = m_meshes.emplace_back();
			m.look = object.look;
			add(object, affine{}, triangles, m.sprites);
			append(triangles, m.batches);
		};
		for(const geometric_object& object : meshes)
			add_mesh(object);
		for(const placed_geometry* card : cards)
		{
			m_level_cards.push_back({ m_meshes.size(), to_mat4(card->world) });
			add_mesh(card->object);
		}
		stats.min = level_min;
		stats.max = level_max;

		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(vertex)), vertices.data(), GL_STATIC_DRAW);
		if(stats.min.x > stats.max.x) // no level vertices
			stats.min = stats.max = glm::vec3(0);
		return stats;
	}

	void level_renderer::draw(const glm::mat4& view, const glm::mat4& projection) const
	{
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE); // which faces are double sided isn't decoded yet
		glUseProgram(m_program);
		glm::mat4 view_projection = projection * view;
		glUniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
		glUniform1f(m_u_edge_clamp, m_edge_clamp ? 1.0f : 0.0f);
		glActiveTexture(GL_TEXTURE0);

		// Where everything goes this frame: cards turn towards the camera.
		const glm::mat4 camera = glm::inverse(view);
		std::vector<part> parts;
		parts.reserve(m_level_cards.size() + m_parts.size());
		for(const auto* list : { &m_level_cards, &m_parts })
		{
			for(const part& p : *list)
			{
				if(p.mesh < m_meshes.size())
					parts.push_back({ p.mesh, face_camera(p.model, m_meshes[p.mesh].look, camera) });
			}
		}

		glBindVertexArray(m_vao);
		const glm::mat4 identity(1.0f);
		auto draw_batches = [this](const std::vector<batch>& batches, blend mode) {
			for(const batch& b : batches)
			{
				if(b.mode != mode)
					continue;
				glBindTexture(GL_TEXTURE_2D, texture_at(b.animation, b.texture));
				glDrawArrays(GL_TRIANGLES, b.first, b.count);
			}
		};
		// Solid surfaces first, the level's and the actors', then the see-through ones over them.
		blend current = blend::none;
		for(blend mode : { blend::none, blend::alpha, blend::additive })
		{
			set_blend(current = mode);
			glUniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(identity));
			draw_batches(m_batches, mode);
			for(const part& p : parts)
			{
				glUniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(p.model));
				draw_batches(m_meshes[p.mesh].batches, mode);
			}
		}
		glUniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(identity));

		// The level's sprites, and the actors' where their parts are now.
		std::vector<sprite_instance> sprites = m_sprites;
		for(sprite_instance& s : sprites)
			s.texture = texture_at(s.animation, s.texture);
		for(const part& p : parts)
		{
			float scale = std::cbrt(std::abs(glm::determinant(glm::mat3(p.model))));
			for(sprite_instance s : m_meshes[p.mesh].sprites)
			{
				s.center = glm::vec3(p.model * glm::vec4(s.center, 1.0f));
				s.texture = texture_at(s.animation, s.texture);
				s.half_size *= scale;
				sprites.push_back(s);
			}
		}
		std::ranges::sort(sprites, [](const sprite_instance& a, const sprite_instance& b) {
			return std::tie(a.mode, a.texture) < std::tie(b.mode, b.texture);
		});

		// Sprites: squares facing the camera, built from its right and up directions. Their normal
		// points at the stand-in sun so they come out fully lit.
		if(!sprites.empty())
		{
			glm::vec3 right(view[0][0], view[1][0], view[2][0]);
			glm::vec3 up(view[0][1], view[1][1], view[2][1]);
			glm::vec3 lit = glm::normalize(glm::vec3(0.3f, 0.2f, 0.9f));
			std::vector<vertex> quads;
			quads.reserve(sprites.size() * 6);
			for(const sprite_instance& s : sprites)
			{
				glm::vec3 x = right * s.half_size.x, y = up * s.half_size.y;
				// v counts up in the game; our textures are uploaded top row first.
				const float u = s.uv_extent.x, v = s.uv_extent.y;
				// A plain sprite shows its texture once; a mirrored one spans two copies, unbounded.
				glm::vec4 bounds{ -1e6f, -1e6f, 1e6f, 1e6f };
				if(u <= 1)
					bounds.x = 0, bounds.z = 1;
				if(v <= 1)
					bounds.y = 0, bounds.w = 1;
				vertex bl{ s.center - x - y, { 0, v }, lit, bounds }, br{ s.center + x - y, { u, v }, lit, bounds };
				vertex tl{ s.center - x + y, { 0, 0 }, lit, bounds }, tr{ s.center + x + y, { u, 0 }, lit, bounds };
				quads.insert(quads.end(), { bl, tl, br, br, tl, tr });
			}
			glBindVertexArray(m_sprite_vao);
			glBindBuffer(GL_ARRAY_BUFFER, m_sprite_vbo);
			glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(quads.size() * sizeof(vertex)), quads.data(), GL_STREAM_DRAW);
			for(std::size_t first = 0; first < sprites.size();)
			{
				std::size_t last = first;
				while(last < sprites.size() && sprites[last].texture == sprites[first].texture && sprites[last].mode == sprites[first].mode)
					last++;
				if(sprites[first].mode != current)
					set_blend(current = sprites[first].mode);
				glBindTexture(GL_TEXTURE_2D, sprites[first].texture);
				glDrawArrays(GL_TRIANGLES, GLint(first * 6), GLsizei((last - first) * 6));
				first = last;
			}
		}

		set_blend(blend::none);
		glBindVertexArray(0);
		glDisable(GL_DEPTH_TEST);
	}
}
