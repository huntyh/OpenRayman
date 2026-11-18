// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#include <doctest/doctest.h>
#include <renderer/submission_state.h>

using namespace openrayman;

TEST_CASE("uniform cache compares bytes and array lengths, and passes oversized values through")
{
	submission::state cache;
	cache.cache = true;
	const std::array<float, 4> a{1, 2, 3, 4}, b{1, 2, 3, 5};
	CHECK(cache.uniform(4, a.data(), sizeof(a)));
	CHECK_FALSE(cache.uniform(4, a.data(), sizeof(a)));
	CHECK(cache.uniform(4, b.data(), sizeof(b)));
	CHECK(cache.uniform(4, b.data(), sizeof(float) * 2));
	CHECK(cache.uniform(4, b.data(), sizeof(b)));
	CHECK(cache.uniform(5, b.data(), sizeof(b))); // distinct uniform location
	CHECK_FALSE(cache.uniform(-1, b.data(), sizeof(b))); // inactive shader uniform
	std::array<float, 64> large{};
	CHECK(cache.uniform(4, large.data(), sizeof(large)));
	CHECK(cache.uniform(4, large.data(), sizeof(large)));
	CHECK(cache.uniform(4, b.data(), sizeof(b))); // oversized upload invalidated the old value
}

namespace
{
	int uniform_calls = 0, program_calls = 0, buffer_calls = 0, enable_calls = 0, disable_calls = 0;
	void GLAD_API_PTR uniform(GLint, GLfloat) { ++uniform_calls; }
	void GLAD_API_PTR program(GLuint) { ++program_calls; }
	void GLAD_API_PTR buffer(GLenum, GLuint) { ++buffer_calls; }
	void GLAD_API_PTR enable(GLenum) { ++enable_calls; }
	void GLAD_API_PTR disable(GLenum) { ++disable_calls; }

	struct mock_driver
	{
		PFNGLUNIFORM1FPROC old_uniform = glad_glUniform1f;
		PFNGLUSEPROGRAMPROC old_program = glad_glUseProgram;
		PFNGLBINDBUFFERPROC old_buffer = glad_glBindBuffer;
		PFNGLENABLEPROC old_enable = glad_glEnable;
		PFNGLDISABLEPROC old_disable = glad_glDisable;
		mock_driver()
		{
			uniform_calls = program_calls = buffer_calls = enable_calls = disable_calls = 0;
			glad_glUniform1f = uniform;
			glad_glUseProgram = program;
			glad_glBindBuffer = buffer;
			glad_glEnable = enable;
			glad_glDisable = disable;
		}
		~mock_driver()
		{
			glad_glUniform1f = old_uniform;
			glad_glUseProgram = old_program;
			glad_glBindBuffer = old_buffer;
			glad_glEnable = old_enable;
			glad_glDisable = old_disable;
		}
	};
}

TEST_CASE("submission cache invalidates at program and renderer boundaries")
{
	mock_driver driver;
	{
		submission::scope scope;
		submission::UseProgram(7);
		submission::Uniform1f(3, 1);
		submission::Uniform1f(3, 1);
		CHECK(uniform_calls == 1);
		submission::UseProgram(7);
		CHECK(program_calls == 1);
		submission::UseProgram(8);
		submission::Uniform1f(3, 1);
		CHECK(uniform_calls == 2);
	}
	{
		submission::scope scope;
		submission::UseProgram(8);
		submission::Uniform1f(3, 1);
		CHECK(uniform_calls == 3);
	}
}

TEST_CASE("array buffer cache suppresses repeats while element buffer binds remain VAO sensitive")
{
	mock_driver driver;
	submission::scope scope;
	submission::BindBuffer(GL_ARRAY_BUFFER, 11);
	submission::BindBuffer(GL_ARRAY_BUFFER, 11);
	CHECK(buffer_calls == 1);
	submission::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 12);
	submission::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 12);
	CHECK(buffer_calls == 3);
	submission::BindBuffer(GL_ARRAY_BUFFER, 13);
	submission::BindBuffer(GL_ARRAY_BUFFER, 11);
	CHECK(buffer_calls == 5);
}

TEST_CASE("blend enable cache starts unknown and preserves transitions")
{
    mock_driver driver;
    submission::scope scope;
    submission::EnableBlend(false);
    submission::EnableBlend(false);
    CHECK(disable_calls == 1);
    submission::EnableBlend(true);
    submission::EnableBlend(true);
    CHECK(enable_calls == 1);
    submission::EnableBlend(false);
    CHECK(disable_calls == 2);
}
