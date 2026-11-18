// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#pragma once

#include <renderer/gl.h>
#include <openrayman/game/profiler.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace openrayman::submission
{
	enum class pass { setup, opaque, actors, cores, shadows, deferred, sprites, particles, sky, hud, count };
	enum class metric { draws, program, texture, blend, uniform, buffer, vao, alpha_draws, additive_draws, count };
	// A fresh cache for each renderer invocation: other renderers may change GL state between calls.
	// This never queries the driver and never assumes state survived a previous frame.
	struct state
	{
		pass stage = pass::setup;
		bool cache = false;
		int blending = 0, blend_enabled = -1;
		std::array<std::array<std::uint64_t, std::size_t(metric::count)>, std::size_t(pass::count)> counts{};
		struct value { std::array<unsigned char, 128> bytes; std::size_t size = 0; };
		// This shader uses fewer than 128 locations; larger driver locations pass through.
		std::array<value, 128> uniforms;
		GLuint texture = ~GLuint(0), program = ~GLuint(0), vao = ~GLuint(0), array_buffer = ~GLuint(0);
		GLenum blend_src = 0, blend_dst = 0;
		void add(metric m) { ++counts[std::size_t(stage)][std::size_t(m)]; }
		bool uniform(GLint at, const void* data, std::size_t bytes)
		{
			if(at < 0) return false;
			if(std::size_t(at) >= uniforms.size()) { add(metric::uniform); return true; }
			// An uncached upload still replaces the driver's previous value at this location.
			if(bytes > 128) { uniforms[std::size_t(at)].size = 0; add(metric::uniform); return true; }
			auto& old = uniforms[std::size_t(at)];
			const bool same = old.size == bytes && std::memcmp(old.bytes.data(), data, bytes) == 0;
			if(!same) { old.size = bytes; std::memcpy(old.bytes.data(), data, bytes); }
			if(cache && same) return false;
			add(metric::uniform); return true;
		}
	};
	inline bool baseline()
	{
		static const bool on = [] { const char* v = std::getenv("OPENRAYMAN_DRAW_BASELINE"); return v && std::strcmp(v, "1") == 0; }();
		return on;
	}
	inline state* active = nullptr; // Renderer runs on the GL context's single main thread.
	inline void stage(pass p) { if(active) active->stage = p; }
	inline void blending(int mode) { if(active) active->blending = mode; }
	class scope
	{
		state local;
		state* previous;
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	public:
		explicit scope(bool cache = true) : previous(active)
		{
			local.cache = cache && !baseline();
			active = &local;
		}
		~scope()
		{
			const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			active = previous;
			if(game::profiler::enabled()) {
				static constexpr game::profiler::counter counters[] = {
					game::profiler::counter::draw_calls, game::profiler::counter::program_binds,
					game::profiler::counter::texture_binds, game::profiler::counter::blend_changes,
					game::profiler::counter::uniform_updates, game::profiler::counter::buffer_binds,
					game::profiler::counter::vao_binds };
				for(const auto& row : local.counts)
					for(std::size_t i = 0; i < std::size(counters); ++i)
						game::profiler::add(counters[i], row[i]);
			}
			static std::ofstream log = [] {
				std::ofstream file;
				if(const char* path = std::getenv("OPENRAYMAN_SUBMISSION_LOG")) {
					file.open(path);
					file << "frame,pass,cpu_ms,draws,program,texture,blend,uniform,buffer,vao,alpha_draws,additive_draws\n";
				}
				return file;
			}();
			static unsigned frame = 0;
			if(log) {
				static constexpr const char* names[] = {"setup", "opaque", "actors", "cores", "shadows", "deferred", "sprites", "particles", "sky", "hud"};
				for(std::size_t i = 0; i < local.counts.size(); ++i) {
					log << frame << ',' << names[i] << ',' << ms;
					for(auto n : local.counts[i]) log << ',' << n;
					log << '\n';
				}
				++frame;
			}
		}
	};
	inline void Uniform1f(GLint at, GLfloat x) { std::array data{x}; if(!active || active->uniform(at, data.data(), sizeof(data))) glUniform1f(at, x); }
	inline void Uniform1i(GLint at, GLint x) { std::array data{x}; if(!active || active->uniform(at, data.data(), sizeof(data))) glUniform1i(at, x); }
	inline void Uniform2f(GLint at, GLfloat x, GLfloat y) { std::array data{x,y}; if(!active || active->uniform(at, data.data(), sizeof(data))) glUniform2f(at, x, y); }
	inline void Uniform3f(GLint at, GLfloat x, GLfloat y, GLfloat z) { std::array data{x,y,z}; if(!active || active->uniform(at, data.data(), sizeof(data))) glUniform3f(at, x, y, z); }
	inline void Uniform4f(GLint at, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { std::array data{x,y,z,w}; if(!active || active->uniform(at, data.data(), sizeof(data))) glUniform4f(at, x, y, z, w); }
	inline void Uniform1iv(GLint at, GLsizei count, const GLint* data) {  if(!active || active->uniform(at, data, sizeof(GLint) * count * 1)) glUniform1iv(at, count, data); }
	inline void Uniform2fv(GLint at, GLsizei count, const GLfloat* data) {  if(!active || active->uniform(at, data, sizeof(GLfloat) * count * 2)) glUniform2fv(at, count, data); }
	inline void Uniform3fv(GLint at, GLsizei count, const GLfloat* data) {  if(!active || active->uniform(at, data, sizeof(GLfloat) * count * 3)) glUniform3fv(at, count, data); }
	inline void Uniform4fv(GLint at, GLsizei count, const GLfloat* data) {  if(!active || active->uniform(at, data, sizeof(GLfloat) * count * 4)) glUniform4fv(at, count, data); }
	inline void UniformMatrix4fv(GLint at, GLsizei count, GLboolean transpose, const GLfloat* data) {  if(!active || active->uniform(at, data, sizeof(GLfloat) * count * 16)) glUniformMatrix4fv(at, count, transpose, data); }

	inline void UseProgram(GLuint program) {
		if(active) {
			if(active->cache && active->program == program) return;
			if(active->program != program) for(auto& u : active->uniforms) u.size = 0;
			active->program = program; active->add(metric::program);
		}
		glUseProgram(program);
	}
	inline void BindTexture(GLenum target, GLuint texture) {
		if(active && target == GL_TEXTURE_2D) {
			if(active->cache && active->texture == texture) return;
			active->texture = texture; active->add(metric::texture);
		}
		glBindTexture(target, texture);
	}
	inline void BindBuffer(GLenum target, GLuint buffer) {
		if(active) {
			// ARRAY_BUFFER is context state; ELEMENT_ARRAY_BUFFER belongs to its VAO.
			if(target == GL_ARRAY_BUFFER) {
				if(active->cache && active->array_buffer == buffer) return;
				active->array_buffer = buffer;
			}
			active->add(metric::buffer);
		}
		glBindBuffer(target, buffer);
	}
	inline void BindVertexArray(GLuint vao) {
		if(active) {
			if(active->cache && active->vao == vao) return;
			active->vao = vao; active->add(metric::vao);
		}
		glBindVertexArray(vao);
	}
    inline void EnableBlend(bool enabled)
    {
        if(active) {
            if(active->cache && active->blend_enabled == int(enabled)) return;
            active->blend_enabled = int(enabled);
            active->add(metric::blend);
        }
        if(enabled) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }

	inline void BlendFunc(GLenum src, GLenum dst) {
		if(active) {
			active->blending = dst == GL_ONE ? 2 : 1;
			if(active->cache && active->blend_src == src && active->blend_dst == dst) return;
			active->blend_src = src; active->blend_dst = dst;
			active->add(metric::blend);
		}
		glBlendFunc(src, dst);
	}
	inline void DrawArrays(GLenum mode, GLint first, GLsizei count) {
		if(active) {
			active->add(metric::draws);
			if(active->blending == 1) active->add(metric::alpha_draws);
			if(active->blending == 2) active->add(metric::additive_draws);
		}
		glDrawArrays(mode, first, count);
	}
	inline void DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
		if(active) {
			active->add(metric::draws);
			if(active->blending == 1) active->add(metric::alpha_draws);
			if(active->blending == 2) active->add(metric::additive_draws);
		}
		glDrawElements(mode, count, type, indices);
	}
}
