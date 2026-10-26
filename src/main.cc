// OpenRayman engine entry point.
//
// Phase 0: opens a window, finds the Rayman 2 data, and lets you browse every texture
// and listen to every music track. This proves the data readers, renderer and audio
// work end to end on each platform before level rendering starts.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <audio/music_player.h>
#include <info.h>
#include <renderer/gl.h>

#include <openrayman/cnt_archive.h>
#include <openrayman/game_data.h>
#include <openrayman/gf_texture.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace openrayman;

namespace
{
	const char* vertex_shader = R"(
		uniform vec4 u_rect; // x0, y0, x1, y1 in clip space
		out vec2 v_uv;
		void main()
		{
			vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
			v_uv = vec2(corner.x, 1.0 - corner.y);
			gl_Position = vec4(mix(u_rect.xy, u_rect.zw, corner), 0.0, 1.0);
		}
	)";

	// Draws the texture over a checkerboard so transparent areas are visible.
	const char* fragment_shader = R"(
		in vec2 v_uv;
		out vec4 o_color;
		uniform sampler2D u_texture;
		void main()
		{
			vec2 cell = floor(gl_FragCoord.xy / 8.0);
			vec3 background = mix(vec3(0.30), vec3(0.42), mod(cell.x + cell.y, 2.0));
			vec4 texel = texture(u_texture, v_uv);
			o_color = vec4(mix(background, texel.rgb, texel.a), 1.0);
		}
	)";

	struct texture_ref
	{
		const cnt_archive* archive;
		const cnt_entry* entry;
	};

	struct app
	{
		SDL_Window* window = nullptr;
		SDL_GLContext context = nullptr;
		gl::api api = gl::api::desktop_gl33;
		GLuint program = 0;
		GLuint vao = 0;
		GLint u_rect = -1;

		std::optional<game_data> game;
		std::vector<std::unique_ptr<cnt_archive>> archives;
		std::vector<texture_ref> textures;
		std::size_t texture_index = 0;
		GLuint texture = 0;
		std::uint32_t texture_width = 0, texture_height = 0;
		std::string texture_error;

		std::unique_ptr<music_player> music;
		std::vector<fs::path> music_files;
		std::optional<std::size_t> music_index;

		// The folder dialog answers from another thread.
		std::mutex picked_mutex;
		std::optional<fs::path> picked_folder;
		bool picking = false;

		Uint64 last_title_update = 0;

		// --screenshot <file>: render one frame, save it as BMP and quit. Used for smoke tests.
		std::optional<fs::path> screenshot;
	};

	fs::path settings_file()
	{
		char* pref = SDL_GetPrefPath("OpenRayman", "OpenRayman");
		fs::path path = fs::path(reinterpret_cast<const char8_t*>(pref)) / "game_folder.txt";
		SDL_free(pref);
		return path;
	}

	std::optional<fs::path> load_saved_folder()
	{
		std::ifstream in(settings_file());
		std::string line;
		if(std::getline(in, line) && !line.empty())
			return fs::path(reinterpret_cast<const char8_t*>(line.c_str()));
		return std::nullopt;
	}

	void save_folder(const fs::path& folder)
	{
		std::ofstream out(settings_file());
		auto utf8 = folder.u8string();
		out << std::string(utf8.begin(), utf8.end()) << "\n";
	}

	void show_error(app& a, const std::string& message)
	{
		SDL_Log("%s", message.c_str());
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenRayman", message.c_str(), a.window);
	}

	std::string format_time(double seconds)
	{
		int s = static_cast<int>(seconds);
		return std::format("{}:{:02}", s / 60, s % 60);
	}

	void update_title(app& a)
	{
		std::string title = std::format("OpenRayman {}", version);
		if(!a.game)
			title += " - press O to choose your Rayman 2 folder";
		else if(!a.textures.empty())
		{
			const texture_ref& ref = a.textures[a.texture_index];
			title += std::format(" - [{}/{}] {}", a.texture_index + 1, a.textures.size(), ref.entry->path);
			if(a.texture_error.empty())
				title += std::format(" ({}x{})", a.texture_width, a.texture_height);
			else
				title += " (ERROR: " + a.texture_error + ")";
		}
		if(a.music && a.music_index)
		{
			title += std::format(" | music [{}/{}] {} {}/{}{}", *a.music_index + 1, a.music_files.size(),
				a.music_files[*a.music_index].stem().string(), format_time(a.music->position()),
				format_time(a.music->length()), a.music->paused() ? " (paused)" : "");
		}
		SDL_SetWindowTitle(a.window, title.c_str());
	}

	void show_texture(app& a, std::size_t index)
	{
		if(a.textures.empty())
			return;
		a.texture_index = index;
		if(a.texture)
		{
			glDeleteTextures(1, &a.texture);
			a.texture = 0;
		}

		const texture_ref& ref = a.textures[index];
		try
		{
			gf_texture gf = decode_gf(ref.archive->read(*ref.entry));
			a.texture = gl::create_texture(gf.width, gf.height, gf.rgba);
			a.texture_width = gf.width;
			a.texture_height = gf.height;
			a.texture_error.clear();
		}
		catch(const std::exception& e)
		{
			a.texture_error = e.what();
		}
		update_title(a);
	}

	void play_music(app& a, std::size_t index)
	{
		if(!a.music || a.music_files.empty())
			return;
		a.music_index = index;
		try
		{
			a.music->play(a.music_files[index]);
		}
		catch(const std::exception& e)
		{
			SDL_Log("Could not play %s: %s", a.music_files[index].string().c_str(), e.what());
		}
		update_title(a);
	}

	bool open_game(app& a, const fs::path& folder)
	{
		try
		{
			auto game = game_data::locate(folder);
			if(!game)
			{
				show_error(a, "No Rayman 2 data found in:\n" + folder.string() +
					"\n\nChoose the folder Rayman 2 is installed in (the one containing Rayman2.exe and Data).");
				return false;
			}

			std::vector<std::unique_ptr<cnt_archive>> archives;
			std::vector<texture_ref> textures;
			for(const fs::path& path : game->texture_archives())
			{
				archives.push_back(std::make_unique<cnt_archive>(path));
				for(const cnt_entry& entry : archives.back()->entries())
					textures.push_back({ archives.back().get(), &entry });
			}

			a.game = std::move(game);
			a.archives = std::move(archives);
			a.textures = std::move(textures);
			a.music_files = a.game->music_files();
			a.music_index.reset();
			if(a.music)
				a.music->stop();
			save_folder(folder);
			show_texture(a, 0);
			return true;
		}
		catch(const std::exception& e)
		{
			show_error(a, "Could not read the Rayman 2 data in " + folder.string() + ":\n" + e.what());
			return false;
		}
	}

	void SDLCALL folder_picked(void* userdata, const char* const* files, int)
	{
		auto& a = *static_cast<app*>(userdata);
		std::lock_guard lock(a.picked_mutex);
		a.picking = false;
		if(files && files[0])
			a.picked_folder = fs::path(reinterpret_cast<const char8_t*>(files[0]));
	}

	void pick_folder(app& a)
	{
		if(a.picking)
			return;
		a.picking = true;
		SDL_ShowOpenFolderDialog(&folder_picked, &a, a.window, nullptr, false);
	}

	bool create_context(app& a)
	{
		// Desktop GL 3.3 core everywhere except mobile, which gets OpenGL ES 3.0.
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
		const gl::api attempts[] = { gl::api::gles30 };
#else
		const gl::api attempts[] = { gl::api::desktop_gl33, gl::api::gles30 };
#endif
		for(gl::api api : attempts)
		{
			if(api == gl::api::desktop_gl33)
			{
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
			}
			else
			{
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
			}

			a.context = SDL_GL_CreateContext(a.window);
			if(!a.context)
				continue;

			int loaded = api == gl::api::desktop_gl33
				? gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))
				: gladLoadGLES2(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
			if(loaded)
			{
				a.api = api;
				return true;
			}
			SDL_GL_DestroyContext(a.context);
			a.context = nullptr;
		}
		return false;
	}

	void draw(app& a)
	{
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(a.window, &w, &h);
		glViewport(0, 0, w, h);
		glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		if(a.texture && w > 0 && h > 0)
		{
			// Fit inside the window with a margin. Scale up by whole steps so pixels stay crisp.
			float fit = std::min((w * 0.9f) / a.texture_width, (h * 0.9f) / a.texture_height);
			float scale = fit >= 1.0f ? std::floor(fit) : fit;
			float half_w = a.texture_width * scale / w;
			float half_h = a.texture_height * scale / h;

			glUseProgram(a.program);
			glUniform4f(a.u_rect, -half_w, -half_h, half_w, half_h);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, a.texture);
			glBindVertexArray(a.vao);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}

		if(a.screenshot)
		{
			SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
			SDL_FlipSurface(surface, SDL_FLIP_VERTICAL); // GL reads bottom-up
			SDL_SaveBMP(surface, a.screenshot->string().c_str());
			SDL_DestroySurface(surface);
		}

		SDL_GL_SwapWindow(a.window);
	}

	void step_texture(app& a, long delta)
	{
		if(a.textures.empty())
			return;
		long count = static_cast<long>(a.textures.size());
		long next = (static_cast<long>(a.texture_index) + delta % count + count) % count;
		show_texture(a, static_cast<std::size_t>(next));
	}

	void step_music(app& a, long delta)
	{
		if(a.music_files.empty())
			return;
		long count = static_cast<long>(a.music_files.size());
		long current = a.music_index ? static_cast<long>(*a.music_index) : (delta > 0 ? -1 : 0);
		play_music(a, static_cast<std::size_t>((current + delta % count + count) % count));
	}
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
	SDL_SetAppMetadata("OpenRayman", version, "game.openrayman");
	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenRayman", SDL_GetError(), nullptr);
		return SDL_APP_FAILURE;
	}

	auto a = std::make_unique<app>();
	a->window = SDL_CreateWindow("OpenRayman", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if(!a->window || !create_context(*a))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenRayman",
			("Could not create an OpenGL 3.3 or OpenGL ES 3.0 window. Make sure your graphics drivers are up to date.\n\n"
				+ std::string(SDL_GetError())).c_str(), nullptr);
		return SDL_APP_FAILURE;
	}
	SDL_GL_SetSwapInterval(1);
	SDL_Log("OpenGL: %s / %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

	try
	{
		a->program = gl::create_program(a->api, vertex_shader, fragment_shader);
	}
	catch(const std::exception& e)
	{
		show_error(*a, e.what());
		return SDL_APP_FAILURE;
	}
	a->u_rect = glGetUniformLocation(a->program, "u_rect");
	glUseProgram(a->program);
	glUniform1i(glGetUniformLocation(a->program, "u_texture"), 0);
	glGenVertexArrays(1, &a->vao);

	a->music = std::make_unique<music_player>();

	// Game folder: --game <folder>, else the last one used, else ask.
	std::optional<fs::path> folder;
	std::optional<long> start_texture;
	for(int n = 1; n + 1 < argc; n++)
	{
		std::string arg = argv[n];
		if(arg == "--game")
			folder = fs::path(reinterpret_cast<const char8_t*>(argv[n + 1]));
		else if(arg == "--screenshot")
			a->screenshot = fs::path(reinterpret_cast<const char8_t*>(argv[n + 1]));
		else if(arg == "--texture")
			start_texture = std::strtol(argv[n + 1], nullptr, 10);
	}
	if(!folder)
		folder = load_saved_folder();

	app& state = *a;
	*appstate = a.release();
	if(!folder || !open_game(state, *folder))
	{
		if(state.screenshot)
			return SDL_APP_FAILURE;
		pick_folder(state);
	}
	if(start_texture)
		step_texture(state, *start_texture);
	update_title(state);
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
	app& a = *static_cast<app*>(appstate);
	switch(event->type)
	{
		case SDL_EVENT_QUIT:
			return SDL_APP_SUCCESS;

		case SDL_EVENT_KEY_DOWN:
			switch(event->key.key)
			{
				case SDLK_ESCAPE: return SDL_APP_SUCCESS;
				case SDLK_RIGHT: step_texture(a, 1); break;
				case SDLK_LEFT: step_texture(a, -1); break;
				case SDLK_PAGEDOWN: case SDLK_DOWN: step_texture(a, 25); break;
				case SDLK_PAGEUP: case SDLK_UP: step_texture(a, -25); break;
				case SDLK_HOME: show_texture(a, 0); break;
				case SDLK_END: if(!a.textures.empty()) show_texture(a, a.textures.size() - 1); break;
				case SDLK_M: step_music(a, 1); break;
				case SDLK_N: step_music(a, -1); break;
				case SDLK_SPACE: if(a.music && a.music_index) { a.music->set_paused(!a.music->paused()); update_title(a); } break;
				case SDLK_O: pick_folder(a); break;
				case SDLK_F11:
					SDL_SetWindowFullscreen(a.window, !(SDL_GetWindowFlags(a.window) & SDL_WINDOW_FULLSCREEN));
					break;
			}
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			step_texture(a, event->wheel.y > 0 ? -1 : 1);
			break;

		case SDL_EVENT_FINGER_UP:
			// Touch: right half for next, left half for previous texture.
			step_texture(a, event->tfinger.x > 0.5f ? 1 : -1);
			break;
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
	app& a = *static_cast<app*>(appstate);

	std::optional<fs::path> picked;
	{
		std::lock_guard lock(a.picked_mutex);
		picked.swap(a.picked_folder);
	}
	if(picked)
		open_game(a, *picked);

	// Keep the music clock in the title ticking.
	Uint64 now = SDL_GetTicks();
	if(a.music_index && now - a.last_title_update > 500)
	{
		a.last_title_update = now;
		update_title(a);
	}

	draw(a);
	return a.screenshot ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult)
{
	std::unique_ptr<app> a(static_cast<app*>(appstate));
	if(!a)
		return;
	a->music.reset();
	if(a->context)
		SDL_GL_DestroyContext(a->context);
	if(a->window)
		SDL_DestroyWindow(a->window);
}
