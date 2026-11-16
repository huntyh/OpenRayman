// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#include <renderer/loading_screen.h>
#include <openrayman/gf_texture.h>
#include <openrayman/texture_table.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace openrayman
{
	loading_screen::loading_screen(std::span<const std::unique_ptr<cnt_archive>> archives, gl::api api)
		: m_archives(archives)
	{
		// Window pixels, top left origin.
		m_program = gl::create_program(api, R"(
			layout(location=0) in vec2 position;
			layout(location=1) in vec4 colour;
			layout(location=2) in vec2 uv;
			uniform vec2 size;
			out vec4 v_colour; out vec2 v_uv;
			void main() { gl_Position = vec4(position.x / size.x * 2.0 - 1.0, 1.0 - position.y / size.y * 2.0, 0, 1); v_colour = colour; v_uv = uv; }
		)", R"(
			in vec4 v_colour; in vec2 v_uv; out vec4 colour;
			uniform sampler2D image;
			void main() { colour = texture(image, v_uv) * v_colour; }
		)");
		glGenVertexArrays(1, &m_vao); glGenBuffers(1, &m_vbo);
		glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		constexpr GLsizei stride = 8 * sizeof(float);
		glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
		glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
		glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
		const std::uint8_t white[] = {255, 255, 255, 255};
		m_white = gl::create_texture(1, 1, white);
	}

	loading_screen::~loading_screen()
	{
		for(auto [path, texture] : m_textures) if(texture) glDeleteTextures(1, &texture);
		glDeleteTextures(1, &m_white); glDeleteBuffers(1, &m_vbo);
		glDeleteVertexArrays(1, &m_vao); glDeleteProgram(m_program);
	}

	bool loading_screen::has_picture(const std::string& name) const
	{
		for(const auto& archive : m_archives)
			if(archive->find(name + ".gf"))
				return true;
		return false;
	}

	GLuint loading_screen::texture(const std::string& path, bool additive)
	{
		auto [it, inserted] = m_textures.try_emplace(path, 0);
		if(!inserted) return it->second;
		for(const auto& archive : m_archives)
			if(const auto* entry = archive->find(path))
			{
				try
				{
					auto image = decode_gf(archive->read(*entry));
					texture_info info; info.name = path;
					// The Lum sprites are colour keyed on black like their materials; additive ones
					// lose values below 8, as the game's 16-bit textures do (card 124).
					if(additive)
					{
						for(std::size_t i = 0; i < image.rgba.size(); i += 4)
							for(std::size_t c = 0; c < 3; c++)
								if(image.rgba[i + c] < 8)
									image.rgba[i + c] = 0;
					}
					else if(path.find('/') != std::string::npos)
					{
						info.flags = texture_flag::color_key;
						apply_color_key(info, image.rgba);
					}
					if(path.find('/') == std::string::npos)
					{
						// The art's box: rows and columns where more than 2% of the pixels aren't black.
						auto lit = [&](std::uint32_t x, std::uint32_t y) {
							const std::uint8_t* p = &image.rgba[(std::size_t(y) * image.width + x) * 4];
							return std::max({p[0], p[1], p[2]}) > 24;
						};
						art_box box{float(image.width), float(image.height), 0, 0, float(image.width), float(image.height)};
						for(std::uint32_t y = 0; y < image.height; y++)
						{
							std::uint32_t n = 0;
							for(std::uint32_t x = 0; x < image.width; x++) n += lit(x, y);
							if(n * 50 > image.width) { box.y0 = std::min(box.y0, float(y)); box.y1 = float(y + 1); }
						}
						for(std::uint32_t x = 0; x < image.width; x++)
						{
							std::uint32_t n = 0;
							for(std::uint32_t y = 0; y < image.height; y++) n += lit(x, y);
							if(n * 50 > image.height) { box.x0 = std::min(box.x0, float(x)); box.x1 = float(x + 1); }
						}
						if(box.x1 <= box.x0 || box.y1 <= box.y0) box = {0, 0, float(image.width), float(image.height), float(image.width), float(image.height)};
						m_boxes[path] = box;
					}
					it->second = gl::create_texture(image.width, image.height, image.rgba);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					// The Lum bodies are a quarter of the sprite (mxy): mirror them across both axes.
					const GLint wrap = path.find("_mxy") != std::string::npos ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
				}
				catch(const std::exception& e) { SDL_Log("Loading screen texture %s: %s", path.c_str(), e.what()); }
				break;
			}
		return it->second;
	}

	void loading_screen::quad(GLuint texture, bool additive, float x, float y, float w, float h, const float colour[4], float u0, float v0, float u1, float v1)
	{
		if(!texture) return;
		const float c0 = colour[0], c1 = colour[1], c2 = colour[2], c3 = colour[3];
		const float v[] = {
			x, y, c0, c1, c2, c3, u0, v0,   x + w, y, c0, c1, c2, c3, u1, v0,   x + w, y + h, c0, c1, c2, c3, u1, v1,
			x, y, c0, c1, c2, c3, u0, v0,   x + w, y + h, c0, c1, c2, c3, u1, v1,   x, y + h, c0, c1, c2, c3, u0, v1 };
		glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
		glBindTexture(GL_TEXTURE_2D, texture);
		glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	void loading_screen::draw(const std::string& picture, float alpha, float lums, float seconds, int width, int height)
	{
		if(width <= 0 || height <= 0 || alpha <= 0) return;
		alpha = std::min(alpha, 1.0f);
		const float W = float(width), H = float(height);
		glViewport(0, 0, width, height);
		glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
		glUseProgram(m_program);
		glUniform2f(glGetUniformLocation(m_program, "size"), W, H);
		glUniform1i(glGetUniformLocation(m_program, "image"), 0);
		glActiveTexture(GL_TEXTURE0); glBindVertexArray(m_vao); glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

		// Black over everything, then the picture's art (without its black band) as large as the
		// window allows at its own shape, nothing cut: wide art fills the width, 4:3 art keeps
		// bars at the sides (user, 2026-11-16).
		const float black[] = {0, 0, 0, alpha}, white[] = {1, 1, 1, alpha};
		quad(m_white, false, 0, 0, W, H, black, 0, 0, 1, 1);
		const std::string path = picture + ".gf";
		const GLuint image = texture(path, false);
		const art_box box = m_boxes.count(path) ? m_boxes[path] : art_box{};
		const float aw = box.x1 - box.x0, ah = box.y1 - box.y0;
		const float scale = std::min(W / aw, H / ah);
		const float cx = (box.x0 + box.x1) / 2, cy = (box.y0 + box.y1) / 2;
		// The whole picture, placed so the art's centre is the window's centre (the rest is black
		// or off screen).
		const float pw = box.width * scale, ph = box.height * scale;
		quad(image, false, W / 2 - cx * scale, H / 2 - cy * scale, pw, ph, white, 0, 0, 1, 1);

		// Three yellow Lums in the bottom right corner (in the black bar on a wide window), hopping
		// one after another like the dots after "Saving" and beating their wings like the save Lum.
		const float body = H * 0.022f; // radius of the body sprite
		const GLuint core = texture("textures_personnages/divers/lums_corps_mxy.gf", false);
		const GLuint glow = texture("textures_personnages/divers/lums_corps_ad_mxy.gf", true);
		static const char* const wing_frames[] = {"01", "02", "03", "02"};
		const GLuint wing = texture(std::string("textures_personnages/divers/lum_aile") + wing_frames[int(seconds / 0.07f) % 4] + "_ad.gf", true);
		const float spacing = body * 3.4f;
		const float lum[] = {1, 1, 1, alpha * std::clamp(lums, 0.0f, 1.0f)};
		if(lum[3] <= 0) return;
		for(int n = 0; n < 3; n++)
		{
			// Resting while they fade in (0.3 s), then hopping one after another.
			const float hop = std::max(0.0f, std::sin(std::max(0.0f, seconds - 0.3f) * 5.0f - float(n) * 0.8f));
			const float cx = W - body * 3.0f - float(2 - n) * spacing;
			const float cy = H - body * 3.4f - body * 1.6f * hop * hop;
			// Wings first (shorter than the body is wide, roots at the core's edge), the glow and
			// the body over them; the same proportions as the save Lum (dialog_renderer).
			const float side = body * 1.5f, overlap = body * 0.25f, gap = body * 0.33f;
			quad(wing, true, cx + gap - overlap, cy - side + overlap, side, side, lum, 0, 0, 1, 1);
			quad(wing, true, cx - side + overlap - gap, cy - side + overlap, side, side, lum, 1, 0, 0, 1);
			const float glow_alpha[] = {1, 1, 1, lum[3] * 0.6f};
			quad(glow, true, cx - body * 1.6f, cy - body * 1.6f, body * 3.2f, body * 3.2f, glow_alpha, 0, 0, 2, 2);
			quad(core, false, cx - body, cy - body, body * 2, body * 2, lum, 0, 0, 2, 2);
		}
	}
}
