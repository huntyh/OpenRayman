#pragma once

#include <renderer/gl.h>

#include <openrayman/cnt_archive.h>
#include <openrayman/geometry.h>
#include <openrayman/light.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openrayman
{
	// Draws a level: its static geometry, and the meshes of its actors wherever they are this
	// frame. Everything is uploaded once per level, grouped into one draw call per texture (per
	// mesh for actors).
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

		// Replaces what's on the GPU with this level's static geometry and the meshes its actors
		// use (in their own space, placed by set_parts). Textures are looked up in `archives`.
		// `sectors` gives the sector of each piece of geometry, whose lights shine on it.
		statistics load(std::span<const placed_geometry> geometry, std::span<const geometric_object> meshes,
			std::span<const std::unique_ptr<cnt_archive>> archives, level_lights lights = {},
			std::span<const std::optional<std::size_t>> sectors = {});

		// A mesh given to load(), placed in the world.
		struct part
		{
			std::size_t mesh = 0;
			glm::mat4 model{ 1 };
			std::optional<std::size_t> sector; // whose lights shine on it; found from where it is if not set
		};

		// What to draw of the actors this frame. Parts naming a mesh that wasn't loaded are skipped.
		void set_parts(std::vector<part> parts) { m_parts = std::move(parts); }

		// The clock for animated textures, in seconds.
		void set_time(double seconds) { m_time = seconds; }

		// Sprites turn to face the camera, so this needs the view on its own.
		void draw(const glm::mat4& view, const glm::mat4& projection) const;

		// Keep each triangle's texture lookups inside the copy of the texture it uses (on by
		// default), or not, for comparing edge seams.
		void set_edge_clamp(bool clamp) { m_edge_clamp = clamp; }
		bool edge_clamp() const { return m_edge_clamp; }

		// The game's lights and fog (on by default), or a plain stand-in light for comparing.
		void set_lighting(bool on) { m_lighting = on; }
		bool lighting() const { return m_lighting; }
		// The game's luminosity setting: 0.5 is neutral, higher is brighter.
		void set_luminosity(float luminosity) { m_luminosity = luminosity; }
		float luminosity() const { return m_luminosity; }

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
			int animation = -1; // an animated texture, which replaces `texture` frame by frame
			glm::vec2 scroll{ 0 }; // texture widths per second, in our UV space
			glm::vec3 ambient{ 0 }, diffuse{ 1 }; // the material's colours, for lighting actors
		};

		// An animated texture: each frame's texture, and when it ends within one loop (seconds).
		struct texture_animation
		{
			std::vector<GLuint> textures;
			std::vector<float> ends;
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
			int animation = -1;
		};

		// A loaded mesh placed per frame (actors' meshes, and the level's cards that turn to the
		// camera): its draw calls, and its sprites in the mesh's own space.
		struct mesh
		{
			std::vector<batch> batches;
			std::vector<sprite_instance> sprites;
			look_at look = look_at::none;
		};

		// The lights a part is drawn with, as the shader takes them.
		static constexpr int max_part_lights = 8;
		struct part_lights
		{
			int count = 0;
			std::array<GLint, max_part_lights> kind{};
			std::array<glm::vec3, max_part_lights> vector{}, color{};
			std::array<glm::vec2, max_part_lights> range{};
		};
		part_lights lights_for(const part& p) const;

		void clear();
		void set_blend(blend mode) const;
		gpu_texture texture_for(const texture_info& info, std::span<const std::unique_ptr<cnt_archive>> archives, statistics& stats);
		// The material's animated texture, uploaded once; -1 if its texture is still.
		int animation_for(const visual_material& material, std::span<const std::unique_ptr<cnt_archive>> archives, statistics& stats);
		// What a batch or sprite shows now.
		GLuint texture_at(int animation, GLuint still) const;

		GLuint m_program = 0;
		GLint m_u_view_projection = -1;
		GLint m_u_model = -1;
		GLint m_u_alpha_cut = -1;
		GLint m_u_edge_clamp = -1;
		GLint m_u_uv_offset = -1;
		GLint m_u_camera = -1, m_u_lighting = -1;
		GLint m_u_light_count = -1, m_u_light_kind = -1, m_u_light_vector = -1, m_u_light_color = -1, m_u_light_range = -1;
		GLint m_u_material_ambient = -1, m_u_material_diffuse = -1, m_u_luminosity = -1;
		GLint m_u_fog_color = -1, m_u_fog = -1, m_u_additive = -1;
		GLuint m_vao = 0, m_vbo = 0;
		GLuint m_sprite_vao = 0, m_sprite_vbo = 0; // rebuilt every frame
		std::vector<sprite_instance> m_sprites;     // of the static geometry, in world space
		GLuint m_white = 0; // for untextured materials
		std::map<std::string, gpu_texture> m_textures; // by CNT path, id 0 if missing
		std::vector<batch> m_batches; // of the static geometry: solid surfaces
		struct see_through
		{
			batch b;
			glm::vec3 center; // drawn farthest first
		};
		std::vector<see_through> m_see_through; // of the static geometry, one per object and texture
		std::vector<mesh> m_meshes;
		std::vector<part> m_parts;
		std::vector<part> m_level_cards; // the level's own cards that turn to the camera
		std::vector<texture_animation> m_animations;
		std::map<std::uint32_t, int> m_animation_by_material; // by visual material address
		double m_time = 0;
		bool m_edge_clamp = true;
		bool m_lighting = true;
		float m_luminosity = 0.5f;
		level_lights m_lights;
	};
}
