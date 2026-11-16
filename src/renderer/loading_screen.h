// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#pragma once

#include <renderer/gl.h>
#include <openrayman/cnt_archive.h>
#include <map>
#include <memory>
#include <span>
#include <string>

namespace openrayman
{
	// Ours: the picture between two levels. The game names one per level (its DSB's vignette,
	// read from Vignette.cnt) and draws it with a loading bar that fills as the level loads. Ours
	// loads too fast for a bar, so the picture fades in over the old level, stays a moment with
	// three yellow Lums hopping under it, and fades out over the new one (user, 2026-11-16;
	// docs/IMPROVEMENTS.md). The art (without the black band most pictures have above and below
	// it) is drawn as large as the window allows at its own shape, nothing cut.
	class loading_screen
	{
	public:
		loading_screen(std::span<const std::unique_ptr<cnt_archive>> archives, gl::api api);
		~loading_screen();
		loading_screen(const loading_screen&) = delete;
		loading_screen& operator=(const loading_screen&) = delete;
		// Whether Vignette.cnt (or another archive) has this picture, e.g. "VigLoadLearn01".
		bool has_picture(const std::string& name) const;
		// Draws `picture` over the whole window at `alpha` (0..1), the Lums at `lums` (0..1, times
		// alpha); `seconds` drives them.
		void draw(const std::string& picture, float alpha, float lums, float seconds, int width, int height);
	private:
		GLuint texture(const std::string& path, bool additive);
		void quad(GLuint texture, bool additive, float x, float y, float w, float h, const float colour[4], float u0, float v0, float u1, float v1);
		std::span<const std::unique_ptr<cnt_archive>> m_archives;
		GLuint m_program = 0, m_vao = 0, m_vbo = 0, m_white = 0;
		int m_width = 1, m_height = 1;
		std::map<std::string, GLuint> m_textures;
		// Where a picture's art is (pixels, x0 y0 x1 y1, of its width and height): most pictures
		// hold a wide band with black above and below inside their 640x480.
		struct art_box { float x0 = 0, y0 = 0, x1 = 1, y1 = 1, width = 1, height = 1; };
		std::map<std::string, art_box> m_boxes;
	};
}
