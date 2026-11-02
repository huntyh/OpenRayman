// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#include <renderer/dialog_renderer.h>
#include <openrayman/gf_texture.h>

#include <algorithm>
#include <cmath>

namespace openrayman
{
	dialog_renderer::dialog_renderer(const memory_image& memory, std::span<const std::unique_ptr<cnt_archive>> archives)
		: m_memory(memory), m_archives(archives)
	{
	}

	dialog_renderer::~dialog_renderer()
	{
		for(auto [path, texture] : m_textures) if(texture) SDL_DestroyTexture(texture);
		if(m_renderer) SDL_DestroyRenderer(m_renderer);
		if(m_surface) SDL_DestroySurface(m_surface);
	}

	std::optional<memory_location> dialog_renderer::location(std::uint32_t address) const
	{
		for(const auto* block : m_memory.blocks())
			if(address >= block->base && std::uint64_t(address) < std::uint64_t(block->base) + block->data.size())
				return memory_location{block, address - block->base};
		return std::nullopt;
	}

	dialog_renderer::font& dialog_renderer::font_for(std::uint32_t actor)
	{
		auto [it, inserted] = m_fonts.try_emplace(actor);
		if(inserted)
			try
			{
				// 0x409500 takes the template's initial list (+0x10), not the current list.
				if(auto at = location(actor))
					if(auto data = m_memory.follow(*at))
						if(auto list = m_memory.follow(*data + 0x10))
							it->second.objects = read_object_list(m_memory, *list);
			}
			catch(const std::exception& e) { SDL_Log("Dialog template 0x%08X: %s", actor, e.what()); }
		return it->second;
	}

	SDL_Texture* dialog_renderer::texture_for(const texture_info& info)
	{
		std::string path = info.cnt_path();
		auto [it, inserted] = m_textures.try_emplace(path, nullptr);
		if(!inserted) return it->second;
		for(const auto& archive : m_archives)
			if(const auto* entry = archive->find(path))
			{
				try
				{
					auto image = decode_gf(archive->read(*entry));
					if(info.flags & texture_flag::color_key)
						for(std::size_t i = 0; i < image.rgba.size(); i += 4)
							if(image.rgba[i] < 8 && image.rgba[i + 1] < 8 && image.rgba[i + 2] < 8) image.rgba[i + 3] = 0;
					it->second = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, int(image.width), int(image.height));
					if(it->second)
					{
						SDL_UpdateTexture(it->second, nullptr, image.rgba.data(), int(image.width * 4));
						SDL_SetTextureBlendMode(it->second, info.flags & texture_flag::additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
						SDL_SetTextureScaleMode(it->second, SDL_SCALEMODE_LINEAR);
					}
				}
				catch(const std::exception& e) { SDL_Log("Dialog texture %s: %s", path.c_str(), e.what()); }
				break;
			}
		return it->second;
	}

	const dialog_renderer::glyph* dialog_renderer::glyph_for(font& f, int index)
	{
		if(index < 0 || std::size_t(index) >= f.objects.size()) return nullptr;
		auto [it, inserted] = f.glyphs.try_emplace(index);
		if(inserted)
			try
			{
				const auto& physical = f.objects[std::size_t(index)].physical_object;
				if(physical)
					if(auto at = physical_object_geometry(m_memory, *physical))
					{
						auto object = read_geometric_object(m_memory, *at);
						if(!object.elements.empty() && object.elements[0].material && object.elements[0].material->texture)
							if(auto* texture = texture_for(*object.elements[0].material->texture))
							{
								std::vector<SDL_FColor> colours(object.vertices.size(), {1, 1, 1, 1});
								// The visual set's colour blocks carry four u16 RGBA channels per
								// vertex. /O selects objects with different painted gradients.
								if(auto visual = m_memory.follow(*physical))
									if(auto blocks = m_memory.follow(*visual + 0x10))
										if(auto first = m_memory.follow(*blocks))
											if(auto vertex_block = m_memory.follow(*first + 4))
												if(auto data = m_memory.follow(*vertex_block + 4))
													if(m_memory.read_u32(*vertex_block) == colours.size())
														for(std::size_t i = 0; i < colours.size(); ++i)
														{
															auto rg = m_memory.read_u32(*data + std::uint32_t(i * 8));
															auto ba = m_memory.read_u32(*data + std::uint32_t(i * 8 + 4));
															colours[i] = {float(rg & 0xffff) / 255, float(rg >> 16) / 255,
																float(ba & 0xffff) / 255, float(ba >> 16) / 255};
														}
								it->second = glyph{std::move(object), texture, std::move(colours)};
							}
					}
			}
			catch(const std::exception& e) { SDL_Log("Dialog glyph %d: %s", index, e.what()); }
		return it->second ? &*it->second : nullptr;
	}

	SDL_Surface* dialog_renderer::draw(std::span<const game::dialog_text> dialogs,
		std::span<const game::particles::screen_rectangle> boxes,
		std::span<const game::particles::screen_image> images,
		const glm::mat4& view_projection, const glm::mat4& projection, int width, int height, double time)
	{
		if(width <= 0 || height <= 0) return nullptr;
		if(!m_surface || m_surface->w != width || m_surface->h != height)
		{
			for(auto [path, texture] : m_textures) if(texture) SDL_DestroyTexture(texture);
			m_textures.clear();
			for(auto& [address, font] : m_fonts) font.glyphs.clear();
			if(m_renderer) SDL_DestroyRenderer(m_renderer);
			if(m_surface) SDL_DestroySurface(m_surface);
			m_surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ABGR8888);
			m_renderer = m_surface ? SDL_CreateSoftwareRenderer(m_surface) : nullptr;
			if(m_renderer) SDL_SetRenderScale(m_renderer, width / 640.0f, height / 480.0f);
		}
		if(!m_renderer) return nullptr;
		SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
		SDL_RenderClear(m_renderer);
		SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
		for(const auto& box : boxes)
		{
			SDL_FRect r{box.first[0] * 6.4f, box.first[1] * 4.8f,
				(box.second[0] - box.first[0]) * 6.4f, (box.second[1] - box.first[1]) * 4.8f};
			SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, Uint8(std::clamp(box.alpha, 0.0f, 255.0f)));
			SDL_RenderFillRect(m_renderer, &r);
		}
		for(const auto& image : images)
			try
			{
				if(auto material = location(image.material))
					if(auto visual = m_memory.follow(*material))
					{
						auto info = read_visual_material(m_memory, *visual);
						if(info.texture)
						{
							const texture_info* frame = &*info.texture;
							float duration = 0;
							for(const auto& f : info.frames) duration += std::max(0.0f, f.duration);
							if(duration > 0)
							{
								float at = float(std::fmod(time, duration));
								for(const auto& f : info.frames)
								{
									frame = &f.texture;
									if(at < f.duration) break;
									at -= std::max(0.0f, f.duration);
								}
							}
							if(auto* texture = texture_for(*frame))
							{
								SDL_SetTextureAlphaModFloat(texture, std::clamp(image.second[2] / 255.0f, 0.0f, 1.0f));
								SDL_FRect r{image.first[0] * 6.4f, image.first[1] * 4.8f,
									(image.second[0] - image.first[0]) * 6.4f, (image.second[1] - image.first[1]) * 4.8f};
								if(frame->tiling & texture_tiling::mirror_u)
								{
									SDL_FRect half{r.x, r.y, r.w * 0.5f, r.h};
									SDL_RenderTexture(m_renderer, texture, nullptr, &half);
									half.x += half.w;
									SDL_RenderTextureRotated(m_renderer, texture, nullptr, &half, 0, nullptr, SDL_FLIP_HORIZONTAL);
								}
								else SDL_RenderTexture(m_renderer, texture, nullptr, &r);
								SDL_SetTextureAlphaModFloat(texture, 1);
							}
						}
					}
			}
			catch(const std::exception& e) { SDL_Log("Screen image: %s", e.what()); }
		for(const auto& dialog : dialogs)
		{
			float x, y, unit;
			if(dialog.screen)
			{
				x = std::trunc(dialog.x) * 0.64f;
				y = std::trunc(dialog.y) * 0.48f;
				// 0x422940 uses reciprocal screen depth. The final glyph magnification is
				// provisional pending a matched capture from the original renderer.
				float depth = std::abs(std::trunc(dialog.z)) * 0.001f;
				unit = 150.0f * projection[1][1] * depth / (0.1f * (1.0f + depth));
			}
			else
			{
				glm::vec4 p = view_projection * glm::vec4(dialog.x, dialog.y, dialog.z, 1);
				if(p.w <= 0) continue;
				x = (p.x / p.w + 1) * 320; y = (1 - p.y / p.w) * 240;
				unit = 240.0f * projection[1][1] / p.w;
			}
			auto layout = game::layout_dialog(dialog);
			if(layout.centred) x = (dialog.screen ? 320 : x) - layout.width * unit * 0.5f;
			else if(dialog.screen && dialog.creator) x += 4.0f; // provisional dialog inset, reference pixels
			auto& font = font_for(dialog.template_actor);
			for(const auto& item : layout.glyphs)
			{
				if(!item.revealed) continue;
				// A null physical object is an intentional blank (including the trailing
				// colon in Murfy's source strings), not a failed glyph load.
				if(item.index >= 0 && std::size_t(item.index) < font.objects.size() &&
					!font.objects[std::size_t(item.index)].physical_object) continue;
				const auto* g = glyph_for(font, item.index);
				if(!g)
				{
					// Missing or invalid glyph data: preserve a readable stand-in.
					char fallback[] = {char(item.byte), 0};
					SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
					SDL_RenderDebugText(m_renderer, x + item.x * unit, y + item.y * unit, fallback);
					continue;
				}
				for(const auto& element : g->object.elements)
				{
					std::vector<SDL_Vertex> vertices;
					for(std::size_t t = 0; t < element.triangles.size(); ++t)
						for(int c = 0; c < 3; ++c)
						{
							const auto& p = g->object.vertices[element.triangles[t][c]];
							const auto& uv = element.uvs[element.uv_triangles[t][c]];
							vertices.push_back({{x + (item.x + item.advance * 0.5f + p[0] * item.scale) * unit,
								y + (item.y - p[2] * item.scale) * unit}, g->colours[element.triangles[t][c]], {uv[0], 1 - uv[1]}});
						}
					if(!vertices.empty()) SDL_RenderGeometry(m_renderer, g->texture, vertices.data(), int(vertices.size()), nullptr, 0);
				}
			}
		}
		SDL_RenderPresent(m_renderer);
		return m_surface;
	}
}
