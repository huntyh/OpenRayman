// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#pragma once

#include <SDL3/SDL.h>
#include <renderer/gl.h>
#include <openrayman/actor.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/game/dialog.h>
#include <openrayman/game/particles.h>
#include <openrayman/geometry.h>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openrayman
{
	// GPU overlay in the game's 640x480 reference space.
	class dialog_renderer
	{
	public:
		dialog_renderer(const memory_image& memory, std::span<const std::unique_ptr<cnt_archive>> archives, gl::api api);
		~dialog_renderer();
		dialog_renderer(const dialog_renderer&) = delete;
		dialog_renderer& operator=(const dialog_renderer&) = delete;
		void draw(std::span<const game::dialog_text> dialogs,
			std::span<const game::particles::screen_rectangle> boxes,
			std::span<const game::particles::screen_image> images,
			const glm::mat4& view_projection, const glm::mat4& projection, int width, int height, double time);
	private:
		struct glyph { geometric_object object; GLuint texture = 0; std::vector<SDL_FColor> colours; };
		struct font { std::vector<actor_object> objects; std::map<int, std::optional<glyph>> glyphs; };
		std::optional<memory_location> location(std::uint32_t address) const;
		font& font_for(std::uint32_t actor);
		const glyph* glyph_for(font& f, int index);
		GLuint texture_for(const texture_info& info);
		const memory_image& m_memory;
		std::span<const std::unique_ptr<cnt_archive>> m_archives;
		GLuint m_program = 0, m_vao = 0, m_vbo = 0, m_white = 0;
		struct batch { GLuint texture; bool additive; std::vector<SDL_Vertex> vertices; };
		std::vector<batch> m_batches;
		void append(GLuint texture, bool additive, std::span<const SDL_Vertex> vertices);
		std::map<std::uint32_t, font> m_fonts;
		std::map<std::string, GLuint> m_textures;
	};
}
