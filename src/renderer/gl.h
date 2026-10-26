#pragma once

#include <glad/gl.h>

#include <cstdint>
#include <span>
#include <string>

namespace openrayman::gl
{
	// Which flavour of OpenGL the context is. Shaders are written for both and only differ in the header.
	enum class api
	{
		desktop_gl33,
		gles30,
	};

	// Compiles and links a vertex + fragment shader pair. Throws std::runtime_error with the log on failure.
	GLuint create_program(api api, const std::string& vertex_source, const std::string& fragment_source);

	// Uploads RGBA8 pixels to a new texture. Rows are top to bottom.
	GLuint create_texture(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba);
}
