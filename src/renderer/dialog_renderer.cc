// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#include <renderer/dialog_renderer.h>
#include <openrayman/gf_texture.h>
#include <openrayman/game/screen_text.h>

#include <algorithm>
#include <format>
#include <array>
#include <cmath>
#include <cstddef>

namespace openrayman
{
	dialog_renderer::dialog_renderer(const memory_image& memory, std::span<const std::unique_ptr<cnt_archive>> archives, gl::api api)
		: m_memory(memory), m_archives(archives)
	{
		m_program = gl::create_program(api, R"(
			layout(location=0) in vec2 position;
			layout(location=1) in vec4 colour;
			layout(location=2) in vec2 uv;
			out vec4 v_colour; out vec2 v_uv;
			void main() { gl_Position = vec4(position.x / 320.0 - 1.0, 1.0 - position.y / 240.0, 0, 1); v_colour = clamp(colour, 0.0, 1.0); v_uv = uv; }
		)", R"(
			in vec4 v_colour; in vec2 v_uv; out vec4 colour;
			uniform sampler2D image;
			void main() { colour = texture(image, v_uv) * v_colour; }
		)");
		glGenVertexArrays(1, &m_vao); glGenBuffers(1, &m_vbo);
		glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SDL_Vertex), reinterpret_cast<void*>(offsetof(SDL_Vertex, position)));
		glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(SDL_Vertex), reinterpret_cast<void*>(offsetof(SDL_Vertex, color)));
		glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SDL_Vertex), reinterpret_cast<void*>(offsetof(SDL_Vertex, tex_coord)));
		const std::uint8_t white[] = {255,255,255,255};
		m_white = gl::create_texture(1, 1, white);
	}

	dialog_renderer::~dialog_renderer()
	{
		for(auto [path, texture] : m_textures) if(texture) glDeleteTextures(1, &texture);
		for(auto [key, texture] : m_joined) if(texture) glDeleteTextures(1, &texture);
		glDeleteTextures(1, &m_white); glDeleteBuffers(1, &m_vbo);
		glDeleteVertexArrays(1, &m_vao); glDeleteProgram(m_program);
	}

	void dialog_renderer::append(GLuint texture, bool additive, std::span<const SDL_Vertex> vertices)
	{
		if(m_batches.empty() || m_batches.back().texture != texture || m_batches.back().additive != additive)
			m_batches.push_back({texture, additive, {}});
		auto& out = m_batches.back().vertices;
		out.insert(out.end(), vertices.begin(), vertices.end());
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

	std::optional<gf_texture> dialog_renderer::decode(const texture_info& info)
	{
		std::string path = info.cnt_path();
		for(const auto& archive : m_archives)
			if(const auto* entry = archive->find(path))
			{
				try
				{
					auto image = decode_gf(archive->read(*entry));
					if(info.flags & texture_flag::color_key)
						for(std::size_t i = 0; i < image.rgba.size(); i += 4)
							if(image.rgba[i] < 8 && image.rgba[i + 1] < 8 && image.rgba[i + 2] < 8) image.rgba[i + 3] = 0;
					return image;
				}
				catch(const std::exception& e) { SDL_Log("Dialog texture %s: %s", path.c_str(), e.what()); }
				break;
			}
		return std::nullopt;
	}

	GLuint dialog_renderer::texture_for(const texture_info& info)
	{
		auto [it, inserted] = m_textures.try_emplace(info.cnt_path(), 0);
		if(!inserted) return it->second;
		if(auto image = decode(info))
		{
			it->second = gl::create_texture(image->width, image->height, image->rgba);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		return it->second;
	}

	namespace
	{
		// Catmull-Rom weights for a sample at fraction t between taps 1 and 2 of four.
		std::array<float, 4> catmull_rom(float t)
		{
			float t2 = t * t, t3 = t2 * t;
			return {(-t3 + 2 * t2 - t) * 0.5f, (3 * t3 - 5 * t2 + 2) * 0.5f, (-3 * t3 + 4 * t2 + t) * 0.5f, (t3 - t2) * 0.5f};
		}

		// Enlarge premultiplied RGBA floats `scale` times along one axis with Catmull-Rom; taps
		// past the edge repeat the edge texel.
		std::vector<float> enlarge(const std::vector<float>& in, int w, int h, int scale, bool along_x)
		{
			int ow = along_x ? w * scale : w, oh = along_x ? h : h * scale;
			int n = along_x ? w : h, across_count = along_x ? h : w;
			std::vector<float> out(std::size_t(ow) * std::size_t(oh) * 4);
			for(int o = 0; o < n * scale; o++)
			{
				float at = (float(o) + 0.5f) / float(scale) - 0.5f;
				int i = int(std::floor(at));
				auto weight = catmull_rom(at - float(i));
				for(int across = 0; across < across_count; across++)
					for(int c = 0; c < 4; c++)
					{
						float sum = 0;
						for(int k = 0; k < 4; k++)
						{
							int j = std::clamp(i - 1 + k, 0, n - 1);
							int x = along_x ? j : across, y = along_x ? across : j;
							sum += weight[std::size_t(k)] * in[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 + std::size_t(c)];
						}
						int x = along_x ? o : across, y = along_x ? across : o;
						out[(std::size_t(y) * std::size_t(ow) + std::size_t(x)) * 4 + std::size_t(c)] = sum;
					}
			}
			return out;
		}
	}

	GLuint dialog_renderer::joined_tiles(std::span<const game::particles::screen_image> tiles)
	{
		// 0x4658B0 draws each tile as its own quad, so the filtering stops at every tile edge and
		// the joins show; and the whole picture is only 6 x 32 by 3 x 32 texels. Ours puts the
		// tiles together into one picture and enlarges it with a smooth cut-out edge, like the
		// screen objects' edges (docs/IMPROVEMENTS.md). Built once per set of tiles.
		std::string key;
		for(const auto& tile : tiles) key += std::format("{}:{:x},", tile.slot, tile.material);
		auto [it, inserted] = m_joined.try_emplace(key, 0);
		if(!inserted) return it->second;
		std::vector<std::pair<int, gf_texture>> images;
		for(const auto& tile : tiles)
		{
			auto material = location(tile.material);
			auto visual = material ? m_memory.follow(*material) : std::nullopt;
			if(!visual) return 0;
			auto info = read_visual_material(m_memory, *visual);
			auto image = info.texture ? decode(*info.texture) : std::nullopt;
			if(!image) return 0;
			// Every tile must be the same size to join them.
			if(!images.empty() && (image->width != images.front().second.width || image->height != images.front().second.height))
				return 0;
			images.emplace_back(tile.slot - 100, std::move(*image));
		}
		if(images.empty()) return 0;
		int tw = int(images.front().second.width), th = int(images.front().second.height);
		int w = tw * 6, h = th * 3;
		// Premultiplied, so the transparent texels' black doesn't bleed into the edge.
		std::vector<float> joined(std::size_t(w) * std::size_t(h) * 4, 0.0f);
		for(const auto& [index, image] : images)
		{
			int left = index / 3 * tw, top = index % 3 * th;
			for(int y = 0; y < th; y++)
				for(int x = 0; x < tw; x++)
				{
					const std::uint8_t* p = &image.rgba[(std::size_t(y) * std::size_t(tw) + std::size_t(x)) * 4];
					float a = p[3] / 255.0f;
					float* q = &joined[(std::size_t(top + y) * std::size_t(w) + std::size_t(left + x)) * 4];
					for(int c = 0; c < 3; c++) q[c] = p[c] / 255.0f * a;
					q[3] = a;
				}
		}
		constexpr int scale = 8;
		auto big = enlarge(enlarge(joined, w, h, scale, true), w * scale, h, scale, false);
		std::vector<std::uint8_t> rgba(big.size());
		// The edge: a smoothstep about one and a half enlarged texels wide around half alpha.
		constexpr float edge = 1.5f / scale;
		for(std::size_t i = 0; i < big.size(); i += 4)
		{
			float a = std::clamp(big[i + 3], 0.0f, 1.0f);
			float t = std::clamp((a - 0.5f + edge) / (2 * edge), 0.0f, 1.0f);
			float shown = t * t * (3 - 2 * t);
			for(std::size_t c = 0; c < 3; c++)
				rgba[i + c] = std::uint8_t(std::clamp(a > 0.001f ? big[i + c] / a : 0.0f, 0.0f, 1.0f) * 255.0f + 0.5f);
			rgba[i + 3] = std::uint8_t(shown * 255.0f + 0.5f);
		}
		it->second = gl::create_texture(std::uint32_t(w * scale), std::uint32_t(h * scale), rgba);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
							if(auto texture = texture_for(*object.elements[0].material->texture))
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
															// 0x431400 converts the signed channels by 1/256; the material
															// conversion at 0x42D520 doubles and clamps RGB before interpolation.
															// The font's diffuse is 1, ambient 0, and its alpha is opaque.
															auto channel = [](std::uint32_t n) { return std::clamp(float(std::int16_t(n)) / 128.0f, 0.0f, 1.0f); };
															colours[i] = {channel(rg & 0xffff), channel(rg >> 16), channel(ba & 0xffff), 1.0f};
														}
								it->second = glyph{std::move(object), texture, std::move(colours)};
							}
					}
			}
			catch(const std::exception& e) { SDL_Log("Dialog glyph %d: %s", index, e.what()); }
		return it->second ? &*it->second : nullptr;
	}

	void dialog_renderer::draw(std::span<const game::dialog_text> dialogs,
		std::span<const game::particles::screen_rectangle> boxes,
		std::span<const game::particles::screen_image> images,
		const glm::mat4& view_projection, const glm::mat4& projection, int width, int height, double time,
		const game::interpreter::view_bounds& bounds, bool show_bars)
	{
		if(width <= 0 || height <= 0) return;
		m_batches.clear();
		auto quad = [this](GLuint texture, bool additive, float x, float y, float w, float h, SDL_FColor colour, float u0, float u1) {
			SDL_Vertex v[] = {{{x,y},colour,{u0,0}}, {{x+w,y},colour,{u1,0}}, {{x+w,y+h},colour,{u1,1}},
				{{x,y},colour,{u0,0}}, {{x+w,y+h},colour,{u1,1}}, {{x,y+h},colour,{u0,1}}};
			append(texture, additive, v);
		};
		if(show_bars)
		{
			float top = std::clamp(bounds.top, 0, 1000) * 0.48f;
			float bottom = std::clamp(bounds.bottom, 0, 1000) * 0.48f;
			quad(m_white, false, 0, 0, 640, top, {0,0,0,1}, 0, 1);
			quad(m_white, false, 0, bottom, 640, 480 - bottom, {0,0,0,1}, 0, 1);
		}
		for(const auto& box : boxes)
		{
			SDL_FRect r{box.first[0] * 6.4f, box.first[1] * 4.8f,
				(box.second[0] - box.first[0]) * 6.4f, (box.second[1] - box.first[1]) * 4.8f};
			quad(m_white, false, r.x, r.y, r.w, r.h, {0,0,0,std::clamp(box.alpha / 255.0f, 0.0f, 1.0f)}, 0, 1);
		}
		// The tiled title picture (slots 100..117) is drawn as one joined picture when it can be.
		std::vector<game::particles::screen_image> tiles;
		for(const auto& image : images)
			if(image.centred && image.slot >= 100 && image.slot < 118) tiles.push_back(image);
		if(!tiles.empty())
		{
			if(GLuint texture = joined_tiles(tiles))
			{
				float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
				for(const auto& tile : tiles)
				{
					int column = (tile.slot - 100) / 3, row = (tile.slot - 100) % 3;
					float tw = tile.second[0] - tile.first[0], th = tile.second[1] - tile.first[1];
					x0 = std::min(x0, tile.first[0] - column * tw); y0 = std::min(y0, tile.first[1] - row * th);
					x1 = std::max(x1, tile.first[0] + (6 - column) * tw); y1 = std::max(y1, tile.first[1] + (3 - row) * th);
				}
				SDL_FRect r{x0 * 6.4f, y0 * 4.8f, (x1 - x0) * 6.4f, (y1 - y0) * 4.8f};
				float area = std::min(1.0f, (4.0f / 3.0f) * height / width);
				r.x = 320.0f + (r.x - 320.0f) * area;
				r.w *= area;
				// Ours: 0.6 of the game's size about the same centre, so the 192x96
				// picture looks less blurry (user request, 2026-11-06).
				constexpr float smaller = 0.6f;
				r.x += r.w * (1 - smaller) * 0.5f; r.y += r.h * (1 - smaller) * 0.5f;
				r.w *= smaller; r.h *= smaller;
				quad(texture, false, r.x, r.y, r.w, r.h, {1, 1, 1, std::clamp(tiles.front().second[2] / 255.0f, 0.0f, 1.0f)}, 0, 1);
			}
			else tiles.clear();
		}
		for(const auto& image : images)
			if(!tiles.empty() && image.centred && image.slot >= 100 && image.slot < 118) continue;
			else try
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
							if(auto texture = texture_for(*frame))
							{
								SDL_FRect r{image.first[0] * 6.4f, image.first[1] * 4.8f,
									(image.second[0] - image.first[0]) * 6.4f, (image.second[1] - image.first[1]) * 4.8f};
								if(image.centred)
								{
									// Like the slot layer: a centred 4:3 area at wide window sizes.
									float area = std::min(1.0f, (4.0f / 3.0f) * height / width);
									r.x = 320.0f + (r.x - 320.0f) * area;
									r.w *= area;
								}
								SDL_FColor colour{1,1,1,std::clamp(image.second[2] / 255.0f, 0.0f, 1.0f)};
								bool additive = (frame->flags & texture_flag::additive) != 0;
								if(frame->tiling & texture_tiling::mirror_u)
								{
									quad(texture, additive, r.x, r.y, r.w * 0.5f, r.h, colour, 0, 1);
									quad(texture, additive, r.x + r.w * 0.5f, r.y, r.w * 0.5f, r.h, colour, 1, 0);
								}
								else quad(texture, additive, r.x, r.y, r.w, r.h, colour, 0, 1);
							}
						}
					}
			}
			catch(const std::exception& e) { SDL_Log("Screen image: %s", e.what()); }
		for(const auto& dialog : dialogs)
		{
			if(dialog.tint)
			{
				// 0x465DD0 / 0x4660B0: screen slots draw texture quads in
				// thousandths of the viewport, independent of camera depth.
				auto layout = game::layout_dialog(dialog);
				auto& font = font_for(dialog.template_actor);
				float left = layout.centred ? (1000.0f - layout.width) * 0.5f : std::trunc(dialog.x);
				int glyph_height = game::screen_slot_metrics(dialog.z).height;
				for(const auto& item : layout.glyphs)
				{
					if(!item.revealed || item.byte == ' ') continue;
					const auto* g = glyph_for(font, item.index);
					if(!g) continue;
					auto tint = *dialog.tint;
					if(item.screen_colour == 0 || item.screen_colour == 600) tint = {110/255.0f, 190/255.0f, 1, tint[3]};
					else if(item.screen_colour == 200) tint = {250/255.0f, 70/255.0f, 70/255.0f, tint[3]};
					else if(item.screen_colour == 400) tint = {1, 1, 120/255.0f, tint[3]};
					float offset = float(game::screen_slot_punctuation_offset(item.byte, glyph_height));
					// Keep the slot layer in a centred 4:3 area at wide window sizes.
					float slot_width = std::min(1.0f, (4.0f / 3.0f) * height / width);
					float x0 = 320.0f + ((left + item.x) * 0.64f - 320.0f) * slot_width;
					float x1 = x0 + item.advance * 0.64f * slot_width;
					float y0 = (std::trunc(dialog.y) + item.y + offset - dialog.pulse) * 0.48f;
					float y1 = (std::trunc(dialog.y) + item.y + offset + glyph_height + dialog.pulse) * 0.48f;
					// 0x429670 passes white at the upper corners and the slot colour
					// at the lower corners to GLI_DRV_vSendSpriteToClipWithCol.
					SDL_FColor top{1, 1, 1, tint[3] * dialog.alpha}, bottom{tint[0], tint[1], tint[2], tint[3] * dialog.alpha};
					SDL_Vertex v[] = {{{x0,y0},top,{0,0}}, {{x1,y0},top,{1,0}}, {{x1,y1},bottom,{1,1}},
						{{x0,y0},top,{0,0}}, {{x1,y1},bottom,{1,1}}, {{x0,y1},bottom,{0,1}}};
					append(g->texture, false, v);
				}
				continue;
			}
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
			std::optional<SDL_FColor> tint;
			if(dialog.tint)
				tint = SDL_FColor{(*dialog.tint)[0], (*dialog.tint)[1], (*dialog.tint)[2], (*dialog.tint)[3]};
			for(const auto& item : layout.glyphs)
			{
				if(!item.revealed) continue;
				// A null physical object is an intentional blank (including the trailing
				// colon in Murfy's source strings), not a failed glyph load.
				if(item.index >= 0 && std::size_t(item.index) < font.objects.size() &&
					!font.objects[std::size_t(item.index)].physical_object) continue;
				const auto* g = glyph_for(font, item.index);
				if(!g) continue; // missing glyph data: nothing to draw
				auto glyph_tint = tint;
				if(glyph_tint && item.screen_colour >= 0)
				{
					float alpha = glyph_tint->a;
					if(item.screen_colour == 0 || item.screen_colour == 600) glyph_tint = SDL_FColor{110 / 255.0f, 190 / 255.0f, 1, alpha};
					else if(item.screen_colour == 200) glyph_tint = SDL_FColor{250 / 255.0f, 70 / 255.0f, 70 / 255.0f, alpha};
					else if(item.screen_colour == 400) glyph_tint = SDL_FColor{1, 1, 120 / 255.0f, alpha};
				}
				for(const auto& element : g->object.elements)
				{
					std::vector<SDL_Vertex> vertices;
					for(std::size_t t = 0; t < element.triangles.size(); ++t)
						for(int c = 0; c < 3; ++c)
						{
							const auto& p = g->object.vertices[element.triangles[t][c]];
							const auto& uv = element.uvs[element.uv_triangles[t][c]];
							SDL_FColor colour = glyph_tint ? *glyph_tint : g->colours[element.triangles[t][c]];
							colour.a *= dialog.alpha; // the text actor's own fade
							vertices.push_back({{x + (item.x + item.advance * 0.5f + p[0] * item.scale) * unit,
								y + (item.y - p[2] * item.scale) * unit}, colour, {uv[0], 1 - uv[1]}});
						}
					if(!vertices.empty()) append(g->texture, element.material && element.material->texture && (element.material->texture->flags & texture_flag::additive), vertices);
				}
			}
		}
		glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glDisable(GL_CULL_FACE);
		glUseProgram(m_program); glUniform1i(glGetUniformLocation(m_program, "image"), 0);
		glActiveTexture(GL_TEXTURE0); glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		for(const auto& batch : m_batches)
		{
			glBlendFunc(GL_SRC_ALPHA, batch.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
			glBindTexture(GL_TEXTURE_2D, batch.texture);
			glBufferData(GL_ARRAY_BUFFER, batch.vertices.size() * sizeof(SDL_Vertex), batch.vertices.data(), GL_STREAM_DRAW);
			glDrawArrays(GL_TRIANGLES, 0, GLsizei(batch.vertices.size()));
		}
	}
}
