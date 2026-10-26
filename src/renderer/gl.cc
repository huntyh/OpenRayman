#include <renderer/gl.h>

#include <stdexcept>
#include <vector>

namespace openrayman::gl
{
	static std::string header(api api)
	{
		if(api == api::gles30)
			return "#version 300 es\nprecision mediump float;\n";
		return "#version 330 core\n";
	}

	static GLuint compile(GLenum type, const std::string& source)
	{
		GLuint shader = glCreateShader(type);
		const char* text = source.c_str();
		glShaderSource(shader, 1, &text, nullptr);
		glCompileShader(shader);

		GLint ok = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if(!ok)
		{
			GLint length = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
			std::vector<char> log(static_cast<std::size_t>(length) + 1);
			glGetShaderInfoLog(shader, length, nullptr, log.data());
			glDeleteShader(shader);
			throw std::runtime_error(std::string("shader compile failed: ") + log.data());
		}
		return shader;
	}

	GLuint create_program(api api, const std::string& vertex_source, const std::string& fragment_source)
	{
		GLuint vs = compile(GL_VERTEX_SHADER, header(api) + vertex_source);
		GLuint fs = compile(GL_FRAGMENT_SHADER, header(api) + fragment_source);
		GLuint program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glLinkProgram(program);
		glDeleteShader(vs);
		glDeleteShader(fs);

		GLint ok = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if(!ok)
		{
			GLint length = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
			std::vector<char> log(static_cast<std::size_t>(length) + 1);
			glGetProgramInfoLog(program, length, nullptr, log.data());
			glDeleteProgram(program);
			throw std::runtime_error(std::string("shader link failed: ") + log.data());
		}
		return program;
	}

	GLuint create_texture(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba)
	{
		GLuint texture = 0;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
			GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return texture;
	}
}
