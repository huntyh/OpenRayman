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
			layout(location = 4) in vec3 a_color; // the game's light, worked out at load (static geometry)
			uniform mat4 u_view_projection;
			uniform mat4 u_model;
			uniform vec3 u_camera;
			// 0: a soft stand-in light from above. 1: the game's lights, from a_color. 2: the game's
			// lights, from the u_light_* list below (actors, which move between lights).
			uniform int u_lighting;
			const int max_lights = 8;
			uniform int u_light_count;
			uniform int u_light_kind[max_lights];    // 0 ambient, 1 parallel, 2 spherical, 3 spherical painting
			uniform vec3 u_material_ambient, u_material_diffuse; // the surface's own colours
			uniform vec3 u_light_vector[max_lights]; // parallel: the way it shines; spherical: its position
			uniform vec3 u_light_color[max_lights];
			uniform vec2 u_light_range[max_lights];  // spherical: near, far
			out vec2 v_uv;
			flat out vec4 v_uv_bounds;
			out vec3 v_light;
			out float v_distance;
			void main()
			{
				vec4 world = u_model * vec4(a_position, 1.0);
				vec3 normal = normalize(mat3(u_model) * a_normal);
				if(u_lighting == 1)
					v_light = a_color;
				else if(u_lighting == 2)
				{
					vec3 sum = vec3(0.0);
					for(int i = 0; i < u_light_count; i++)
					{
						float strength = 1.0;
						if(u_light_kind[i] == 1)
							strength = max(0.0, -dot(normal, u_light_vector[i]));
						else if(u_light_kind[i] >= 2)
						{
							vec3 to = u_light_vector[i] - world.xyz;
							float d = length(to);
							vec2 range = u_light_range[i];
							float falloff = d <= range.x || range.y <= range.x ? 1.0 : clamp((range.y - d) / (range.y - range.x), 0.0, 1.0);
							strength = falloff * (d > 1e-5 && u_light_kind[i] == 2 ? max(0.0, dot(normal, to / d)) : 1.0);
						}
						sum += u_light_color[i] * strength;
					}
					v_light = clamp(u_material_ambient + sum * u_material_diffuse, 0.0, 1.0);
				}
				else
				{
					// Stand-in lighting, to compare with: a soft light from above, so shapes are readable.
					vec3 sun = normalize(vec3(0.3, 0.2, 0.9));
					v_light = vec3(0.6 + 0.4 * abs(dot(normal, sun)));
				}
				v_uv = a_uv;
				v_uv_bounds = a_uv_bounds;
				v_distance = length(world.xyz - u_camera);
				gl_Position = u_view_projection * world;
			}
		)";

		// Hard-edged alpha (leaves, ferns) is cut out; soft alpha is blended in a second pass.
		const char* fragment_shader = R"(
			in vec2 v_uv;
			in vec3 v_light;
			in float v_distance;
			flat in vec4 v_uv_bounds;
			out vec4 o_color;
			uniform sampler2D u_texture;
			uniform float u_alpha_cut;
			uniform float u_edge_clamp;
			uniform vec2 u_uv_offset; // scrolling textures (water, mist, clouds)
			uniform vec4 u_fog_color;
			uniform vec4 u_fog;       // near, far, how much fog covers at near and at far (0..1)
			uniform float u_additive; // additive surfaces fade out in fog instead of taking its colour
			void main()
			{
				// A triangle that stays within one copy of its texture keeps half a texel inside
				// that copy's edges, so filtering doesn't bring in the opposite edge as a line.
				// From afar the smaller mipmap copies are used, whose texels are larger, so the margin
				// follows the mip level in use (rounded up, as trilinear filtering blends two levels).
				vec2 uv = v_uv;
				vec2 uv_dx = dFdx(v_uv), uv_dy = dFdy(v_uv);
				if(u_edge_clamp > 0.5)
				{
					vec2 size = vec2(textureSize(u_texture, 0));
					vec2 dx = uv_dx * size, dy = uv_dy * size;
					float lod = max(0.0, ceil(0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8))));
					vec2 extent = v_uv_bounds.zw - v_uv_bounds.xy;
					vec2 half_texel = min(0.5 * exp2(lod) / size, 0.5 * extent);
					uv = clamp(uv, v_uv_bounds.xy + half_texel, v_uv_bounds.zw - half_texel);
				}
				// The level of detail comes from the unclamped coordinates, so clamping doesn't change it.
				// Scrolling textures tile seamlessly, so their offset goes on after the clamp.
				vec4 texel = textureGrad(u_texture, uv + u_uv_offset, uv_dx, uv_dy);
				if(texel.a < u_alpha_cut)
					discard;
				vec3 color = texel.rgb * v_light;
				if(u_fog.y > u_fog.x)
				{
					float fog = mix(u_fog.z, u_fog.w, clamp((v_distance - u_fog.x) / (u_fog.y - u_fog.x), 0.0, 1.0));
					color = u_additive > 0.5 ? color * (1.0 - fog) : mix(color, u_fog_color.rgb, fog);
				}
				o_color = vec4(color, u_alpha_cut > 0.25 ? 1.0 : texel.a);
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
			glm::vec3 color{ 1.0f };                          // the game's light on this corner
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

		// The game's scroll speeds are per frame; its clock runs at 60 frames per second (assumed,
		// compare with the game).
		constexpr float scroll_frames_per_second = 60.0f;

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
		m_u_uv_offset = glGetUniformLocation(m_program, "u_uv_offset");
		m_u_alpha_cut = glGetUniformLocation(m_program, "u_alpha_cut");
		m_u_edge_clamp = glGetUniformLocation(m_program, "u_edge_clamp");
		m_u_camera = glGetUniformLocation(m_program, "u_camera");
		m_u_lighting = glGetUniformLocation(m_program, "u_lighting");
		m_u_light_count = glGetUniformLocation(m_program, "u_light_count");
		m_u_light_kind = glGetUniformLocation(m_program, "u_light_kind");
		m_u_light_vector = glGetUniformLocation(m_program, "u_light_vector");
		m_u_light_color = glGetUniformLocation(m_program, "u_light_color");
		m_u_light_range = glGetUniformLocation(m_program, "u_light_range");
		m_u_material_ambient = glGetUniformLocation(m_program, "u_material_ambient");
		m_u_material_diffuse = glGetUniformLocation(m_program, "u_material_diffuse");
		m_u_fog_color = glGetUniformLocation(m_program, "u_fog_color");
		m_u_fog = glGetUniformLocation(m_program, "u_fog");
		m_u_additive = glGetUniformLocation(m_program, "u_additive");
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
			glEnableVertexAttribArray(4);
			glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, color)));
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
		m_lights = {};
	}

	void level_renderer::set_blend(blend mode) const
	{
		// See-through surfaces go over the solid scene, without hiding each other.
		if(mode == blend::none)
		{
			glDisable(GL_BLEND);
			glDepthMask(GL_TRUE);
			glUniform1f(m_u_alpha_cut, 0.5f);
			glUniform1f(m_u_additive, 0.0f);
			return;
		}
		glEnable(GL_BLEND);
		glDepthMask(GL_FALSE);
		glUniform1f(m_u_alpha_cut, 0.01f);
		glUniform1f(m_u_additive, mode == blend::additive ? 1.0f : 0.0f);
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
		std::span<const std::unique_ptr<cnt_archive>> archives, level_lights lights, std::span<const std::optional<std::size_t>> sectors)
	{
		clear();
		m_lights = std::move(lights);
		statistics stats;
		stats.min = glm::vec3(1e30f);
		stats.max = glm::vec3(-1e30f);

		// Triangles grouped by texture. Corners are expanded into separate vertices, because a
		// corner's UV is indexed separately from its position.
		using by_texture = std::map<std::tuple<blend, GLuint, int, std::array<float, 2>, std::array<float, 6>>, std::vector<vertex>>;
		// Static geometry is lit once here, by the lights its sector lists for scenery; meshes placed
		// per frame (`lit` empty) are lit as they're drawn.
		auto add = [&](const geometric_object& object, const affine& world, by_texture& triangles, std::vector<sprite_instance>& sprites,
			std::span<const light* const> lit) {
			stats.meshes++;
			for(const triangle_element& element : object.elements)
			{
				gpu_texture texture{ m_white, blend::none };
				int animation = -1;
				std::array<float, 2> scroll{};
				// The material's ambient colour is its least light; lights add on top, times its diffuse colour.
				std::array<float, 6> colors{ 0, 0, 0, 1, 1, 1 };
				if(element.material)
				{
					const visual_material& m = *element.material;
					colors = { m.ambient[0], m.ambient[1], m.ambient[2], m.diffuse[0], m.diffuse[1], m.diffuse[2] };
				}
				if(element.material && element.material->texture)
				{
					texture = texture_for(*element.material->texture, archives, stats);
					if(!texture.id)
						texture = { m_white, blend::none };
					animation = animation_for(*element.material, archives, stats);
					scroll = element.material->scroll;
				}
				std::vector<vertex>& out = triangles[{ texture.mode, texture.id, animation, scroll, colors }];
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
						glm::vec3 color(1.0f);
						if(!lit.empty())
						{
							// The normal turns with the object, without moving.
							vec3 origin = world.apply({ 0, 0, 0 }), tip = world.apply(n);
							glm::vec3 turned = glm::normalize(glm::vec3(tip[0] - origin[0], tip[1] - origin[1], tip[2] - origin[2]));
							vec3 sum = light_at(lit, light_target::scenery, p, { turned.x, turned.y, turned.z });
							color = glm::clamp(glm::vec3(colors[0], colors[1], colors[2]) + glm::vec3(sum[0], sum[1], sum[2]) * glm::vec3(colors[3], colors[4], colors[5]), 0.0f, 1.0f);
						}
						out.push_back({ position, uvs[k], glm::vec3(n[0], n[1], n[2]), bounds, color });
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
				// The game scrolls per frame of its 60 Hz clock; our v runs the other way.
				const std::array<float, 2>& scroll = std::get<3>(key);
				glm::vec2 per_second(scroll[0] * scroll_frames_per_second, -scroll[1] * scroll_frames_per_second);
				const std::array<float, 6>& c = std::get<4>(key);
				batches.push_back({ std::get<1>(key), GLint(vertices.size()), GLsizei(list.size()), std::get<0>(key), std::get<2>(key), per_second,
					glm::vec3(c[0], c[1], c[2]), glm::vec3(c[3], c[4], c[5]) });
				vertices.insert(vertices.end(), list.begin(), list.end());
			}
		};

		by_texture level_triangles;
		std::vector<std::size_t> cards;
		for(std::size_t g = 0; g < geometry.size(); g++)
		{
			const placed_geometry& placed = geometry[g];
			if(placed.object.look != look_at::none)
			{
				cards.push_back(g);
				continue;
			}
			std::vector<const light*> lit;
			if(g < sectors.size() && sectors[g] && *sectors[g] < m_lights.sectors.size())
			{
				for(std::size_t i : m_lights.sectors[*sectors[g]].lights)
					lit.push_back(&m_lights.lights[i]);
			}
			add(placed.object, placed.world, level_triangles, m_sprites, lit);
		}
		append(level_triangles, m_batches);
		// Meshes placed per frame are in their own space, so they don't count towards the level's bounds.
		glm::vec3 level_min = stats.min, level_max = stats.max;
		auto add_mesh = [&](const geometric_object& object) {
			by_texture triangles;
			mesh& m = m_meshes.emplace_back();
			m.look = object.look;
			add(object, affine{}, triangles, m.sprites, {});
			append(triangles, m.batches);
		};
		for(const geometric_object& object : meshes)
			add_mesh(object);
		for(std::size_t g : cards)
		{
			std::optional<std::size_t> sector = g < sectors.size() ? sectors[g] : std::nullopt;
			m_level_cards.push_back({ m_meshes.size(), to_mat4(geometry[g].world), sector });
			add_mesh(geometry[g].object);
		}
		stats.min = level_min;
		stats.max = level_max;

		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(vertex)), vertices.data(), GL_STATIC_DRAW);
		if(stats.min.x > stats.max.x) // no level vertices
			stats.min = stats.max = glm::vec3(0);
		return stats;
	}

	level_renderer::part_lights level_renderer::lights_for(const part& p) const
	{
		part_lights out;
		glm::vec3 at(p.model[3]);
		vec3 position{ at.x, at.y, at.z };
		std::optional<std::size_t> sector = p.sector ? p.sector : m_lights.sector_at(position);
		if(!sector || *sector >= m_lights.sectors.size())
			return out;
		// How strongly each light reaches the part's origin, ignoring which way its surfaces face.
		std::vector<std::pair<float, const light*>> candidates;
		for(std::size_t i : m_lights.sectors[*sector].lights)
		{
			const light& l = m_lights.lights[i];
			if(!l.on || !(l.targets & light_target::actors) || l.type == light_type::fog)
				continue;
			float reach = 1;
			if(l.type == light_type::spherical)
			{
				vec3 c = l.position();
				float d = glm::length(glm::vec3(c[0], c[1], c[2]) - at);
				reach = d <= l.near ? 1.0f : d >= l.far ? 0.0f : (l.far - d) / (l.far - l.near);
			}
			float size = std::abs(l.color[0]) + std::abs(l.color[1]) + std::abs(l.color[2]);
			if(reach > 0 && size > 0)
				candidates.push_back({ reach * size, &l });
		}
		std::ranges::sort(candidates, [](const auto& a, const auto& b) { return a.first > b.first; });
		for(const auto& [strength, l] : candidates)
		{
			if(out.count == max_part_lights)
				break;
			std::size_t i = std::size_t(out.count++);
			vec3 v = l->type == light_type::spherical ? l->position() : l->direction();
			out.kind[i] = l->type == light_type::ambient ? 0 : l->type != light_type::spherical ? 1 : l->painting ? 3 : 2;
			out.vector[i] = glm::vec3(v[0], v[1], v[2]);
			out.color[i] = glm::vec3(l->color[0], l->color[1], l->color[2]);
			out.range[i] = glm::vec2(l->near, l->far);
		}
		return out;
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
		const glm::vec3 eye(camera[3]);
		glUniform3f(m_u_camera, eye.x, eye.y, eye.z);
		std::vector<part> parts;
		parts.reserve(m_level_cards.size() + m_parts.size());
		for(const auto* list : { &m_level_cards, &m_parts })
		{
			for(const part& p : *list)
			{
				if(p.mesh < m_meshes.size())
					parts.push_back({ p.mesh, face_camera(p.model, m_meshes[p.mesh].look, camera), p.sector });
			}
		}

		// The fog of the sector the camera is in.
		glUniform4f(m_u_fog, 0, 0, 0, 0);
		if(m_lighting)
		{
			if(auto s = m_lights.sector_at({ eye.x, eye.y, eye.z }))
			{
				if(const light* fog = m_lights.fog(*s))
				{
					glUniform4f(m_u_fog_color, fog->color[0], fog->color[1], fog->color[2], 1);
					glUniform4f(m_u_fog, fog->near, fog->far, fog->fog_near_blend, fog->fog_far_blend);
				}
			}
		}

		// The lights on each part: its sector's, the strongest few where it stands.
		std::vector<part_lights> part_light(parts.size());
		if(m_lighting)
		{
			for(std::size_t i = 0; i < parts.size(); i++)
				part_light[i] = lights_for(parts[i]);
		}

		glBindVertexArray(m_vao);
		const glm::mat4 identity(1.0f);
		auto use_lights = [this](const part_lights& l) {
			glUniform1i(m_u_light_count, l.count);
			if(l.count == 0)
				return;
			glUniform1iv(m_u_light_kind, l.count, l.kind.data());
			glUniform3fv(m_u_light_vector, l.count, glm::value_ptr(l.vector[0]));
			glUniform3fv(m_u_light_color, l.count, glm::value_ptr(l.color[0]));
			glUniform2fv(m_u_light_range, l.count, glm::value_ptr(l.range[0]));
		};
		auto draw_batches = [this](const std::vector<batch>& batches, blend mode) {
			for(const batch& b : batches)
			{
				if(b.mode != mode)
					continue;
				glBindTexture(GL_TEXTURE_2D, texture_at(b.animation, b.texture));
				// Wrapped every 2 widths, which keeps mirrored textures the same and the numbers small.
				glm::vec2 offset(std::fmod(double(b.scroll.x) * m_time, 2.0), std::fmod(double(b.scroll.y) * m_time, 2.0));
				glUniform2f(m_u_uv_offset, offset.x, offset.y);
				glUniform3fv(m_u_material_ambient, 1, glm::value_ptr(b.ambient));
				glUniform3fv(m_u_material_diffuse, 1, glm::value_ptr(b.diffuse));
				glDrawArrays(GL_TRIANGLES, b.first, b.count);
			}
		};
		// Solid surfaces first, the level's and the actors', then the see-through ones over them.
		blend current = blend::none;
		for(blend mode : { blend::none, blend::alpha, blend::additive })
		{
			set_blend(current = mode);
			glUniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(identity));
			glUniform1i(m_u_lighting, m_lighting ? 1 : 0);
			draw_batches(m_batches, mode);
			glUniform1i(m_u_lighting, m_lighting ? 2 : 0);
			for(std::size_t i = 0; i < parts.size(); i++)
			{
				glUniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(parts[i].model));
				use_lights(part_light[i]);
				draw_batches(m_meshes[parts[i].mesh].batches, mode);
			}
		}
		glUniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(identity));
		// Sprites are glows and flames: they shine by themselves.
		glUniform1i(m_u_lighting, m_lighting ? 1 : 0);
		glUniform2f(m_u_uv_offset, 0, 0);

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
