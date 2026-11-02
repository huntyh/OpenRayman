// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#pragma once

#include <SDL3/SDL.h>
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
	// A viewport-sized surface with coordinates in the game's 640x480 reference space.
	class dialog_renderer
	{
	public:
		dialog_renderer(const memory_image& memory, std::span<const std::unique_ptr<cnt_archive>> archives);
		~dialog_renderer();
		dialog_renderer(const dialog_renderer&) = delete;
		dialog_renderer& operator=(const dialog_renderer&) = delete;
		SDL_Surface* draw(std::span<const game::dialog_text> dialogs,
			std::span<const game::particles::screen_rectangle> boxes,
			std::span<const game::particles::screen_image> images,
			const glm::mat4& view_projection, const glm::mat4& projection, int width, int height, double time);
	private:
		struct glyph { geometric_object object; SDL_Texture* texture = nullptr; std::vector<SDL_FColor> colours; };
		struct font { std::vector<actor_object> objects; std::map<int, std::optional<glyph>> glyphs; };
		std::optional<memory_location> location(std::uint32_t address) const;
		font& font_for(std::uint32_t actor);
		const glyph* glyph_for(font& f, int index);
		SDL_Texture* texture_for(const texture_info& info);
		const memory_image& m_memory;
		std::span<const std::unique_ptr<cnt_archive>> m_archives;
		SDL_Surface* m_surface = nullptr;
		SDL_Renderer* m_renderer = nullptr;
		std::map<std::uint32_t, font> m_fonts;
		std::map<std::string, SDL_Texture*> m_textures;
	};
}
