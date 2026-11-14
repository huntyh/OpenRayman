// OpenRayman engine entry point.
//
// Phase 2: opens a window, finds the Rayman 2 data and shows a level's static geometry with a
// free-fly camera. The Phase 0 texture browser and music player are still there (T, M).

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <chrono>

#include <audio/music_player.h>
#include <audio/sound_player.h>
#include <camera.h>
#include <info.h>
#include <renderer/gl.h>
#include <renderer/dialog_renderer.h>
#include <renderer/level_renderer.h>
#include <game_trace.h>

#include <openrayman/actor.h>
#include <openrayman/game/saves.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/game/first_visit.h>
#include <openrayman/game/handlers.h>
#include <openrayman/game/dialog.h>
#include <openrayman/game/interpreter.h>
#include <openrayman/game/input.h>
#include <openrayman/game/live_memory.h>
#include <openrayman/game/profiler.h>
#include <openrayman/game/sound_slots.h>
#include <openrayman/game/sound_placement.h>
#include <openrayman/animation.h>
#include <openrayman/brain.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/collision.h>
#include <openrayman/input.h>
#include <openrayman/game_data.h>
#include <openrayman/geometry.h>
#include <openrayman/gf_texture.h>
#include <openrayman/level_header.h>
#include <openrayman/light.h>
#include <openrayman/memory_image.h>
#include <openrayman/scene.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <set>
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

	// Transparent overlays made by the profiler and dialog renderer.
	const char* text_fragment_shader = R"(
		in vec2 v_uv;
		out vec4 o_color;
		uniform sampler2D u_texture;
		void main() { o_color = texture(u_texture, v_uv); }
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
		GLuint text_program = 0;
		GLuint vao = 0;
		GLint u_rect = -1;

		// Vsync (--vsync, F9): on waits for the monitor but shows a late frame at once instead of a
		// whole refresh later (adaptive, where the driver offers it; plain vsync where not), strict
		// always waits, off never does. --fps-limit caps the frame rate (0: no cap); with adaptive sync
		// monitors a cap a little under the refresh rate keeps the monitor following every frame.
		enum class vsync_mode { on, strict, off };
		vsync_mode vsync = vsync_mode::on;
		int fps_limit = 0;
		Uint64 next_frame_ns = 0;
		// The profiler (game/profiler.h): F8 shows its last report, drawn into a texture,
		// OPENRAYMAN_PROFILE=<file.csv> logs one row a second. The GPU's time comes from timer
		// queries (desktop OpenGL only), read a few frames later so asking never stalls.
		bool show_profile = false;
		std::ofstream profile_log;
		GLuint profile_texture = 0;
		int profile_width = 0, profile_height = 0;
		std::array<GLuint, 4> gpu_queries{};
		int gpu_query_next = 0, gpu_queries_pending = 0;
		bool gpu_timing = false;
		Uint64 frame_started_ns = 0;

		std::optional<game_data> game;
		std::vector<std::unique_ptr<cnt_archive>> archives;
		std::vector<texture_ref> textures;
		std::size_t texture_index = 0;
		GLuint texture = 0;
		std::uint32_t texture_width = 0, texture_height = 0;
		std::string texture_error;

		std::unique_ptr<music_player> music;
		// The sounds the scripts and animations ask for, and the actors that asked (their voices
		// follow where they are).
		std::unique_ptr<sound_player> sounds;
		std::set<std::uint32_t> sound_sources;
		std::string actor_draw_trace; // OPENRAYMAN_DRAW_TRACE: the parts submitted for this draw
		// Sectors play sounds when Rayman walks in (the music of Learn_10's hand-over): their boxes,
		// super objects and data, and the sector he was last in.
		openrayman::level_lights sound_sectors;
		std::vector<std::uint32_t> sound_sector_super_objects, sound_sector_data;
		std::optional<std::size_t> rayman_sector;
		std::vector<fs::path> music_files;
		std::optional<std::size_t> music_index;

		// The folder dialog answers from another thread.
		std::mutex picked_mutex;
		std::optional<fs::path> picked_folder;
		bool picking = false;

		Uint64 last_title_update = 0;

		// --screenshot <file>: render one frame, save it as BMP and quit. Used for smoke tests.
		std::optional<fs::path> screenshot;

		// Level view, the default once a game folder is open.
		enum class view { level, textures };
		view mode = view::level;
		std::unique_ptr<level_renderer> level;
		std::unique_ptr<memory_image> dialog_memory;
		std::unique_ptr<dialog_renderer> dialog_overlay;
		std::uint32_t screen_font_actor = 0;
		std::size_t level_index = 0;
		std::string start_level = "Learn_10"; // --level <name>
		level_renderer::statistics level_stats;
		std::string level_error;
		fly_camera camera;
		std::optional<fly_camera> start_camera; // --camera x,y,z,yaw,pitch (degrees)
		// Actors play their current animation, looping, from when the level loads.
		animation_bank bank;
		actor_set actors;
		double animation_time = 0;        // seconds; --time <seconds> starts there
		std::optional<double> start_time;
		std::optional<std::array<float, 4>> start_place; // --place x,y,z,yaw[@seconds]: reproducible scene captures
		long start_place_tick = 0; // the tick --place applies at (0: before the first)
		openrayman::game_trace trace; // OPENRAYMAN_TRACE=<file>: Rayman every tick, in r2live's format
		std::string trace_vars; // --trace-vars
		bool animating = true;            // F3 pauses
		// The game's scripts, run on a live copy of the level's memory at the game's 60 ticks per
		// second. G switches them off (actors then keep their saved states).
		std::unique_ptr<openrayman::game::live_memory> live;
		std::unique_ptr<openrayman::game::interpreter> vm;
		// The keyboard, fed to the game's input actions while playing (Tab switches between that
		// and flying the camera).
		std::unique_ptr<openrayman::game::input> input;
		bool playing = false;
		bool chose_view = false; // a level was loaded once: later loads keep playing/flying as it is
		// The gamepads SDL knows about, by the instance id its events carry, each with its handle
		// while it is open. main.cc maps SDL's standard buttons and sticks onto the game's pad
		// numbers (pad_map, docs/formats/input.md).
		struct pad_slot
		{
			SDL_JoystickID id = 0;
			SDL_Gamepad* handle = nullptr;
			std::size_t pad = 0; // the game's pad number: the first pad open is 0
			mutable bool start = false; // Start, fed as the Esc key
		};
		std::vector<pad_slot> pads;
		// --keys Up:60-200,A:100-110: keys held over script ticks, to replay input for screenshots.
		struct held_key { std::uint8_t key; long from, to; };
		std::vector<held_key> held_keys;
		unsigned save_slot = 0; // --save 1..3: restore the installed slot at level load
		bool new_game = false; // --start new: levels start as on a new game, not as reached from the hub
		bool intro_start = false; // run the level's opening scene
		int bars_in_place = 0; // ticks left in which a new game's first cutscene bars start fully in (our choice)
		std::vector<std::int32_t> story_bits; // --story-bits 1095,1143: more of a save's story, set at load
		// The story-bit words a level change carries over (Fix stays loaded in the game), set when the
		// scripts ask for another level (procedures 86/87) and used by the next load.
		std::optional<std::vector<std::uint32_t>> carried_story;
		std::optional<std::array<std::uint8_t, 4>> carried_entry;
		openrayman::save_registry registry;
		std::optional<std::vector<std::byte>> carried_general;
		std::map<std::size_t, std::vector<std::byte>> level_progress;
		std::vector<std::uint32_t> fix_actors; // the loaded level's placed Fix actors (super objects)
		long tick = 0;
		std::vector<std::uint32_t> thinkers;     // super objects of the actors with a brain
		// The game's camera actor (the placed Fix actor with a camera part). Until the game's camera
		// runs (milestone 3c) it stands where ours is, for the scripts that work around it.
		std::uint32_t camera_super_object = 0;
		std::ofstream camera_log; // --camera-log: one row per gameplay tick
		std::optional<glm::vec3> camera_log_previous;
		std::vector<std::uint32_t> actor_at;     // per actors.actors entry: its actor in live memory
		std::vector<std::uint32_t> super_object_at; // per entry: its super object
		// Per entry: where each of its family's states is in live memory. (The entries' own
		// locations point into the level files, which are gone once the level has loaded.)
		std::vector<std::vector<std::uint32_t>> state_addresses;
		std::vector<double> state_since;         // per entry: animation time its state began
		// Our smoothing after a switch (actor_set::entry::blend): per entry, the state, animation and
		// frame drawn last time, and the pose being faded out of, if any.
		struct loop_watch
		{
			std::optional<std::size_t> state;
			std::optional<std::uint16_t> animation;
			double frame = 0;
			bool blending = false;
			std::uint16_t from_animation = 0;
			double from_frame = 0, start = 0;
		};
		std::map<std::size_t, loop_watch> loop_watches;
		// Actors the scripts create (function 66, Rayman's shots): what each template looks like, read
		// at load since the renderer takes every mesh then, and per pool slot the entry that shows it.
		struct generated_look
		{
			std::uint32_t model = 0; // the template actor
			actor_set::entry entry;  // its family, states and shadow; meshes filled per object list
			std::vector<std::uint32_t> state_addresses; // copied while the level files still exist
			std::map<std::uint32_t, std::pair<std::vector<std::optional<std::size_t>>, std::vector<std::optional<vec3>>>> lists;
		};
		std::vector<generated_look> generated_looks;
		decltype(generated_look::lists) scene_object_lists;
		std::vector<std::uint32_t> selected_object_lists;
		std::vector<std::optional<std::size_t>> generated_entry; // per pool slot
		bool scripts = true;
		double script_clock = 0;                 // seconds not yet run as ticks
		std::string actor_note;           // the state last picked with F4/F5, shown in the title
		// F10 (Shift+F10 with small pictures): a debug recording of what happens, for the agent to
		// read afterwards. A folder next to the program: every drawn frame (camera, every actor's
		// state, animation, frame, flags, position), Rayman and the held keys every tick, sounds.
		struct debug_recording
		{
			fs::path folder;
			std::ofstream frames, sounds;
			std::unique_ptr<openrayman::game_trace> rayman;
			bool pictures = false;
			long last_picture = -1;
			bool active() const { return frames.is_open(); }
		} recording;
		std::vector<std::pair<std::size_t, std::size_t>> start_states; // --actor-state <node>:<state>
		std::vector<std::pair<std::size_t, std::size_t>> start_behaviours; // --behaviour <node>:<index>
		bool show_cutscene_bars = true; // renderer option, reserved for a future user setting
		bool show_collision = false; // C, --collision on
		float speed = 15.0f;                     // units per second
		bool looking = false;
		Uint64 last_frame = 0;
		bool silent_run = false; // the --time catch-up at load: its sounds aren't played
	};

	fs::path settings_file()
	{
		char* pref = SDL_GetPrefPath("OpenRayman", "OpenRayman");
		if(!pref)
			return {}; // The settings directory may be unavailable (for example in a sandbox).
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

	const char* vsync_name(app::vsync_mode mode)
	{
		return mode == app::vsync_mode::on ? "on" : mode == app::vsync_mode::strict ? "strict" : "off";
	}

	// Swap interval -1 is adaptive vsync; drivers without it refuse, and plain vsync stands in.
	void apply_vsync(app& a)
	{
		bool set = a.vsync == app::vsync_mode::on && SDL_GL_SetSwapInterval(-1);
		if(!set)
			SDL_GL_SetSwapInterval(a.vsync == app::vsync_mode::off ? 0 : 1);
		int interval = 0;
		SDL_GL_GetSwapInterval(&interval);
		SDL_Log("Vsync %s (swap interval %d%s)", vsync_name(a.vsync), interval,
			a.vsync == app::vsync_mode::on && interval == 1 ? ", the driver has no adaptive vsync" : "");
	}

	// The GPU's time for the level: start a timer query on a free slot, after collecting the ones
	// that have finished.
	void begin_gpu_timer(app& a)
	{
		if(!game::profiler::enabled() || a.api != gl::api::desktop_gl33)
			return;
		if(!a.gpu_queries[0])
			glGenQueries(GLsizei(a.gpu_queries.size()), a.gpu_queries.data());
		const int slots = int(a.gpu_queries.size());
		while(a.gpu_queries_pending > 0)
		{
			GLuint query = a.gpu_queries[std::size_t((a.gpu_query_next - a.gpu_queries_pending + slots) % slots)];
			GLint ready = 0;
			glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &ready);
			if(!ready)
				break;
			GLuint64 ns = 0;
			glGetQueryObjectui64v(query, GL_QUERY_RESULT, &ns);
			game::profiler::add(game::profiler::zone::gpu, std::chrono::nanoseconds(ns));
			a.gpu_queries_pending--;
		}
		if(a.gpu_queries_pending == slots)
			return; // all still running: this frame goes untimed
		glBeginQuery(GL_TIME_ELAPSED, a.gpu_queries[std::size_t(a.gpu_query_next)]);
		a.gpu_timing = true;
	}

	void end_gpu_timer(app& a)
	{
		if(!a.gpu_timing)
			return;
		glEndQuery(GL_TIME_ELAPSED);
		a.gpu_query_next = (a.gpu_query_next + 1) % int(a.gpu_queries.size());
		a.gpu_queries_pending++;
		a.gpu_timing = false;
	}

	// A profiler report as lines of text: the frame, each zone's average and worst frame in
	// milliseconds, then the counters per frame.
	std::vector<std::string> profile_lines(const app& a, const game::profiler::report& r)
	{
		using game::profiler::zone;
		using game::profiler::counter;
		auto at = [&](zone z) { return std::size_t(z); };
		std::vector<std::string> lines;
		lines.push_back(std::format("{:.0f} fps  {:.2f} ms  worst {:.2f}", r.frames / r.seconds, r.average_ms[at(zone::frame)], r.worst_ms[at(zone::frame)]));
		#ifdef NDEBUG
		constexpr const char* build_kind = "release";
#else
		constexpr const char* build_kind = "debug build (about 6x slower)";
#endif
		lines.push_back(std::format("vsync {}  cap {}  {}", vsync_name(a.vsync), a.fps_limit ? std::to_string(a.fps_limit) : "off", build_kind));
		lines.push_back("");
		lines.push_back("              avg   worst");
		for(std::size_t i = 1; i < std::size_t(zone::count); i++)
		{
			zone z = zone(i);
			// Parts of the scripts' ticks are indented under them.
			bool part = z >= zone::brains && z <= zone::particles;
			std::string label = (part ? (z == zone::actor_floor ? "    " : "  ") : "") + std::string(game::profiler::name(z));
			lines.push_back(std::format("{:<12}{:>6.2f}{:>8.2f}", label, r.average_ms[i], r.worst_ms[i]));
		}
		lines.push_back("");
		for(std::size_t i = 0; i < std::size_t(counter::count); i++)
			lines.push_back(std::format("{:<18}{:>8.1f}", game::profiler::name(counter(i)), r.per_frame[i]));
		return lines;
	}

	// The report as a texture, 8-pixel glyphs on a dark backing, drawn each frame until the next.
	void update_profile_texture(app& a, const std::vector<std::string>& lines)
	{
		std::size_t longest = 0;
		for(const std::string& line : lines)
			longest = std::max(longest, line.size());
		const int w = int(longest) * 8 + 8, h = int(lines.size()) * 10 + 6;
		SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
		SDL_Renderer* text = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
		if(text)
		{
			SDL_SetRenderDrawColor(text, 0, 0, 0, 170);
			SDL_RenderClear(text);
			SDL_SetRenderDrawColor(text, 230, 230, 230, 255);
			for(std::size_t i = 0; i < lines.size(); i++)
				SDL_RenderDebugText(text, 4, 4 + float(i) * 10, lines[i].c_str());
			SDL_RenderPresent(text);
			if(a.profile_texture)
				glDeleteTextures(1, &a.profile_texture);
			a.profile_texture = gl::create_texture(std::uint32_t(w), std::uint32_t(h),
				std::span(static_cast<const std::uint8_t*>(surface->pixels), std::size_t(w) * std::size_t(h) * 4));
			a.profile_width = w;
			a.profile_height = h;
			SDL_DestroyRenderer(text);
		}
		if(surface)
			SDL_DestroySurface(surface);
	}

	void draw_profile(app& a, int w, int h)
	{
		if(!a.show_profile || !a.profile_texture || w <= 0 || h <= 0)
			return;
		// Whole pixels, twice the size on big windows; top left.
		const float scale = h >= 1000 ? 2.0f : 1.0f;
		const float right = -1 + 2 * a.profile_width * scale / w, bottom = 1 - 2 * a.profile_height * scale / h;
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glUseProgram(a.text_program);
		glUniform4f(glGetUniformLocation(a.text_program, "u_rect"), -1, bottom, right, 1);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, a.profile_texture);
		glBindVertexArray(a.vao);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}

	// Once a second: the overlay's text and the log's row.
	void take_profile(app& a)
	{
		auto report = game::profiler::take_report();
		if(!report)
			return;
		if(a.show_profile)
			update_profile_texture(a, profile_lines(a, *report));
		if(a.profile_log)
		{
			using game::profiler::zone;
			using game::profiler::counter;
			a.profile_log << std::format("{:.3f},{},{}", report->seconds, report->frames, vsync_name(a.vsync));
			for(std::size_t i = 0; i < std::size_t(zone::count); i++)
				a.profile_log << std::format(",{:.3f},{:.3f}", report->average_ms[i], report->worst_ms[i]);
			for(std::size_t i = 0; i < std::size_t(counter::count); i++)
				a.profile_log << std::format(",{:.1f}", report->per_frame[i]);
			a.profile_log << "\n";
			a.profile_log.flush();
		}
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
		else if(a.mode == app::view::level)
		{
			const auto& levels = a.game->game_dsb().levels;
			title += std::format(" - [{}/{}] {}", a.level_index + 1, levels.size(), levels[a.level_index]);
			if(a.level_error.empty())
				title += std::format(" ({} triangles)", a.level_stats.triangles);
			else
				title += " (ERROR: " + a.level_error + ")";
			title += " | WASD move, hold right mouse to look, PgUp/PgDn level, T textures, P copy position, L lighting, F6/F7 luminosity, F4/F5 actor state, G scripts";
			if(!a.actor_note.empty())
				title += " | " + a.actor_note;
			if(a.recording.active())
				title += " | RECORDING (F10 to stop)";
		}
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

	// The game reads the keyboard through DirectInput, whose scan codes number the keys by where they
	// are on a US keyboard, as SDL's do. 0 for keys the game has no code for.
	std::uint8_t game_key(SDL_Scancode s)
	{
		static const std::uint8_t letters[26] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
			0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C }; // A..Z
		static const std::uint8_t keypad[10] = { 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52 }; // 1..9, 0
		if(s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)
			return letters[s - SDL_SCANCODE_A];
		if(s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0)
			return std::uint8_t(0x02 + (s - SDL_SCANCODE_1));
		if(s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10)
			return std::uint8_t(0x3B + (s - SDL_SCANCODE_F1));
		if(s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_0)
			return keypad[s - SDL_SCANCODE_KP_1];
		switch(s)
		{
		case SDL_SCANCODE_ESCAPE: return 0x01;
		case SDL_SCANCODE_MINUS: return 0x0C;
		case SDL_SCANCODE_EQUALS: return 0x0D;
		case SDL_SCANCODE_BACKSPACE: return 0x0E;
		case SDL_SCANCODE_TAB: return 0x0F;
		case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
		case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
		case SDL_SCANCODE_RETURN: return 0x1C;
		case SDL_SCANCODE_LCTRL: return 0x1D;
		case SDL_SCANCODE_SEMICOLON: return 0x27;
		case SDL_SCANCODE_APOSTROPHE: return 0x28;
		case SDL_SCANCODE_GRAVE: return 0x29;
		case SDL_SCANCODE_LSHIFT: return 0x2A;
		case SDL_SCANCODE_BACKSLASH: return 0x2B;
		case SDL_SCANCODE_COMMA: return 0x33;
		case SDL_SCANCODE_PERIOD: return 0x34;
		case SDL_SCANCODE_SLASH: return 0x35;
		case SDL_SCANCODE_RSHIFT: return 0x36;
		case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
		case SDL_SCANCODE_LALT: return 0x38;
		case SDL_SCANCODE_SPACE: return 0x39;
		case SDL_SCANCODE_CAPSLOCK: return 0x3A;
		case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
		case SDL_SCANCODE_SCROLLLOCK: return 0x46;
		case SDL_SCANCODE_KP_MINUS: return 0x4A;
		case SDL_SCANCODE_KP_PLUS: return 0x4E;
		case SDL_SCANCODE_KP_PERIOD: return 0x53;
		case SDL_SCANCODE_F11: return 0x57;
		case SDL_SCANCODE_F12: return 0x58;
		case SDL_SCANCODE_KP_ENTER: return 0x9C;
		case SDL_SCANCODE_RCTRL: return 0x9D;
		case SDL_SCANCODE_KP_DIVIDE: return 0xB5;
		case SDL_SCANCODE_RALT: return 0xB8;
		case SDL_SCANCODE_HOME: return 0xC7;
		case SDL_SCANCODE_UP: return 0xC8;
		case SDL_SCANCODE_PAGEUP: return 0xC9;
		case SDL_SCANCODE_LEFT: return 0xCB;
		case SDL_SCANCODE_RIGHT: return 0xCD;
		case SDL_SCANCODE_END: return 0xCF;
		case SDL_SCANCODE_DOWN: return 0xD0;
		case SDL_SCANCODE_PAGEDOWN: return 0xD1;
		case SDL_SCANCODE_INSERT: return 0xD2;
		case SDL_SCANCODE_DELETE: return 0xD3;
		default: return 0;
		}
	}

	void run_ticks(app& a, double seconds);
	void sync_generated(app& a);

	// Opens a pad SDL just announced, gives it the first free game pad number, and closes it when
	// it goes away or when the viewer quits.
	void open_pad(app& a, SDL_JoystickID id)
	{
		if(std::ranges::any_of(a.pads, [&](const app::pad_slot& p) { return p.id == id; }))
			return;
		SDL_Gamepad* handle = SDL_OpenGamepad(id);
		if(!handle)
		{
			SDL_Log("Could not open gamepad %u: %s", unsigned(id), SDL_GetError());
			return;
		}
		std::size_t pad = 0;
		while(std::ranges::any_of(a.pads, [&](const app::pad_slot& p) { return p.pad == pad; }))
			pad++;
		if(pad >= openrayman::game::input::pad_count)
		{
			SDL_Log("More than %zu gamepads; %s is left alone", openrayman::game::input::pad_count, SDL_GetGamepadName(handle));
			SDL_CloseGamepad(handle);
			return;
		}
		a.pads.push_back({ id, handle, pad });
		SDL_Log("Gamepad %zu: %s", pad, SDL_GetGamepadName(handle));
	}

	void close_pad(app& a, SDL_JoystickID id)
	{
		auto found = std::ranges::find_if(a.pads, [&](const app::pad_slot& p) { return p.id == id; });
		if(found == a.pads.end())
			return;
		if(a.input)
			a.input->release_pad(found->pad);
		if(found->handle)
			SDL_CloseGamepad(found->handle);
		a.pads.erase(found);
	}

	void close_pads(app& a)
	{
		for(const app::pad_slot& p : a.pads)
			if(p.handle)
				SDL_CloseGamepad(p.handle);
		a.pads.clear();
	}

	// How SDL's standard gamepad maps onto the game's pad 0, from what the game's input actions test
	// (`ortool input`, docs/formats/input.md): button 11 jumps (and goes back in menus), 10 shoots
	// (and confirms), 12/13 turn the camera (Q/W), 15 is the J action, 14 or axis 6 pushed below -50
	// is action 12 (as Ctrl and Z); axes 4/5 move, -100..100 for a full push.
	namespace pad_map
	{
		constexpr std::pair<SDL_GamepadButton, std::size_t> buttons[] = {
			{ SDL_GAMEPAD_BUTTON_SOUTH, 11 },          // A: jump
			{ SDL_GAMEPAD_BUTTON_WEST, 10 },           // X: shoot
			{ SDL_GAMEPAD_BUTTON_EAST, 10 },           // B: shoot too (and confirm in menus)
			{ SDL_GAMEPAD_BUTTON_NORTH, 15 },          // Y: the J action
			{ SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 12 },  // camera left (Q)
			{ SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 13 }, // camera right (W); both: action 18 (Num0)
			{ SDL_GAMEPAD_BUTTON_LEFT_STICK, 14 },     // action 12
			{ SDL_GAMEPAD_BUTTON_RIGHT_STICK, 14 },
			{ SDL_GAMEPAD_BUTTON_DPAD_UP, 15 },        // the menus' up (action 101)
			{ SDL_GAMEPAD_BUTTON_DPAD_DOWN, 14 },      // the menus' down (action 102)
		};

		// About 15% of the stick's travel counts as centred, so a worn stick doesn't walk Rayman.
		constexpr std::int32_t dead_zone = 32768 * 15 / 100;

		// A stick's value with the dead zone around 0, rising from 0 at its edge to full.
		inline std::int32_t with_dead_zone(std::int32_t value)
		{
			std::int32_t magnitude = value < 0 ? -value : value;
			if(magnitude <= dead_zone)
				return 0;
			std::int32_t scaled = (magnitude - dead_zone) * 32767 / (32767 - dead_zone);
			return value < 0 ? -scaled : scaled;
		}
	}

	// Feeds one open pad's whole state into the game's pad, read from SDL after any of its events:
	// the buttons above (the d-pad's up/down are the menus' buttons 15/14, as the data has them);
	// the left stick as axes 4/5 (up is negative, as in DirectInput), the d-pad's left/right
	// overriding axis 4; either trigger as axis 6 pushed negative; Start as the Esc key (pause).
	void feed_pad(app& a, const app::pad_slot& p)
	{
		if(!a.input || !a.playing)
			return;
		auto pressed = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(p.handle, b); };
		std::array<bool, openrayman::game::input::pad_button_count> down{};
		for(const auto& [sdl, game] : pad_map::buttons)
			down[game] = down[game] || pressed(sdl);
		auto axis = [&](SDL_GamepadAxis sdl) { return std::int32_t(SDL_GetGamepadAxis(p.handle, sdl)); };
		// Our choice, as in most games: the right stick pushed past half way left or right turns
		// the camera like Q / W, within what the level's camera allows. Pushed right it presses button
		// 12 (Q), left 13 (W): Q swings the camera so the view turns right (user, 2026-11-14).
		const std::int32_t turn = axis(SDL_GAMEPAD_AXIS_RIGHTX);
		down[12] = down[12] || turn > 16384;
		down[13] = down[13] || turn < -16384;
		for(std::size_t b = 0; b < down.size(); b++)
			a.input->set_pad_button(p.pad, b, down[b]);
		std::int32_t x = pad_map::with_dead_zone(axis(SDL_GAMEPAD_AXIS_LEFTX));
		std::int32_t y = pad_map::with_dead_zone(axis(SDL_GAMEPAD_AXIS_LEFTY));
		if(pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) x = -32768;
		if(pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) x = 32767;
		a.input->set_pad_axis(p.pad, 4, x);
		a.input->set_pad_axis(p.pad, 5, y);
		// SDL's triggers read 0..32767; the game wants the combined trigger axis below -50%.
		std::int32_t trigger = std::max(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
		a.input->set_pad_axis(p.pad, 6, -trigger);
		if(p.pad == 0 && pressed(SDL_GAMEPAD_BUTTON_START) != p.start)
		{
			p.start = !p.start;
			a.input->set_key(0x01, p.start); // Esc: only on a change, so the keyboard's Esc still works
		}
	}

	// Runs the game's scripts for `seconds`, in ticks of 1/60 s, then shows the states they picked.
	// An error outside one actor's scripts (input, placing the actors) is logged and the scripts
	// stop, rather than closing the viewer.
	void load_level(app& a, std::size_t index);

	// Procedures 86/87 asked for another level (the menu's New Game asks for Jail_10): load it as
	// a new game would reach it, with the story words the scripts left in Fix.
	void change_requested_level(app& a)
	{
		if(!a.vm || a.vm->level_request.empty() || !a.game)
			return;
		std::string name = a.vm->level_request;
		a.vm->level_request.clear();
		const auto& levels = a.game->game_dsb().levels;
		auto same = [&](const std::string& level) {
			return std::ranges::equal(level, name, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
		};
		auto found = std::ranges::find_if(levels, same);
		if(found == levels.end())
		{
			SDL_Log("The scripts asked for level %s, which Game.dsb doesn't list", name.c_str());
			return;
		}
		a.carried_story = a.vm->story_words(a.fix_actors);
		a.carried_entry = a.vm->menu_request;
		a.carried_general = openrayman::game::capture_save_entries(*a.live, a.registry.general, a.registry.general_size);
		a.level_progress[a.level_index] = openrayman::game::capture_save_entries(*a.live, a.registry.level, a.registry.level_size);
		// A loaded slot seeds this session once. Subsequent loads keep the progress just made.
		a.save_slot = 0;
		a.new_game = true;
		load_level(a, std::size_t(found - levels.begin()));
	}

	void run_scripts(app& a, double seconds)
	{
		try
		{
			run_ticks(a, seconds);
		}
		catch(const std::exception& e)
		{
			SDL_Log("Scripts stopped at tick %ld: %s", a.tick, e.what());
			a.scripts = false;
			a.actor_note = "scripts stopped (see log)";
			update_title(a);
		}
	}

	// Whether the camera's sector hears an actor's (0x40A1F0, the actor sound type's per-listener
	// callback, called from 0x4520A0 once per microphone): yes for Rayman's own sounds (0x500578),
	// in the same sector, or when the camera sector's hearing list (sector data +0x48 first
	// element, +0x50 count; element: sector super object, flag, next) doesn't name the actor's
	// sector; no when it names it with flag 0. The distance curve never goes below 77 of 127, so
	// this is what keeps the chute fan (Jail_20 node 29) out of the starting cell (user,
	// 2026-11-08). The actor's sector is the one the wake pass follows; the camera's is the
	// smallest box that holds it (the renderer's), not yet the exe's own camera sector tracking.
	bool camera_hears(const app& a, std::uint32_t source)
	{
		if(!a.vm || !a.live || source == a.vm->main_actor())
			return true;
		const std::uint32_t from = a.vm->actor_sector(source);
		const auto camera = a.sound_sectors.sector_at({ a.camera.position.x, a.camera.position.y, a.camera.position.z });
		if(!from || !camera || *camera >= a.sound_sector_super_objects.size())
			return true;
		const std::uint32_t listener = a.sound_sector_super_objects[*camera];
		const std::uint32_t data = a.sound_sector_data[*camera];
		if(from == listener || !data || !a.live->contains(data + 0x48, 12))
			return true;
		std::uint32_t element = a.live->u32(data + 0x48);
		for(std::uint32_t n = a.live->u32(data + 0x50); n && element && a.live->contains(element, 12); n--, element = a.live->u32(element + 8))
			if(a.live->u32(element) == from)
				return a.live->u32(element + 4) != 0;
		return true;
	}

	// 0x459D30 / 0x459D80 supply the distance curve, camera_hears the sector gate. An actor may carry
	// its own pair of distances (actor +0x2C: saturation, fade, 16.16), which 0x415930 (the actor
	// sound type's +0x5C callback) hands on through 0x451F30 (info flag 0x10); the wave DLL's
	// _SND_fn_vParam3Dto2D (WAVMWBVR 0x1000B1E0) then uses them instead of Snd_Cpa.ini's 8 and 30.
	// Most carry a fade past 8 x saturation, so their sound does fall to nothing: the Jail_20 chute
	// fan (node 29: 8 and 80) is silent beyond 64 units, out of the starting cell, where the default
	// curve held it at 77/127 (user, 2026-11-08). Panning and the camera listener remain
	// approximations of the DLL's listener callbacks.
	std::pair<float, float> sound_placement(const app& a, std::uint32_t source)
	{
		auto placed = a.live ? openrayman::game::global_placement(*a.live, source) : std::nullopt;
		if(!placed)
			return { 1.0f, 0.0f };
		if(!camera_hears(a, source))
			return { 0.0f, 0.0f };
		glm::vec3 to = glm::vec3(placed->m[0][3], placed->m[1][3], placed->m[2][3]) - a.camera.position;
		float distance = glm::length(to);
		float saturation = 8, fade = 30;
		if(std::uint32_t actor = a.vm ? a.vm->actor_of(source) : 0; actor && a.live->contains(actor + 0x2C, 4))
			if(std::uint32_t own = a.live->u32(actor + 0x2C); own && a.live->contains(own, 8))
			{
				saturation = float(std::int32_t(a.live->u32(own))) / 65536.0f;
				fade = float(std::int32_t(a.live->u32(own + 4))) / 65536.0f;
			}
		float gain = game::sound_distance_volume(distance, saturation, fade) / 127.0f;
		glm::vec3 right = glm::cross(a.camera.forward(), glm::vec3(0, 0, 1));
		float pan = distance > 1e-3f && glm::length(right) > 1e-4f ? 0.8f * glm::dot(to / distance, glm::normalize(right)) : 0.0f;
		return { gain, pan };
	}

	// The sector Rayman is in, as the scripts' wake pass follows him (0x4128D0: the sector he was in
	// and its neighbours, by priority), the same sector the exe keeps per actor. The lighting's
	// smallest box that holds him is only the fallback before that pass has run. With the smallest
	// box a jump into the Jail_20 chute came into it from the wrong sector, and the entry that starts
	// the chute music (from node 42 only) was skipped (user, 2026-11-08).
	std::optional<std::size_t> rayman_sector_now(const app& a)
	{
		if(a.vm)
			if(std::uint32_t followed = a.vm->actor_sector(a.vm->main_actor()))
			{
				auto i = std::ranges::find(a.sound_sector_super_objects, followed);
				if(i != a.sound_sector_super_objects.end())
					return std::size_t(i - a.sound_sector_super_objects.begin());
			}
		auto placed = a.live && a.vm ? openrayman::game::global_placement(*a.live, a.vm->main_actor()) : std::nullopt;
		if(!placed)
			return std::nullopt;
		return a.sound_sectors.sector_at({ placed->m[0][3], placed->m[1][3], placed->m[2][3] });
	}

	// When Rayman moves to another sector, that sector's sound list plays the entries for the sector
	// he came from or for any (0x412880: sector data +0x54 first element, +0x5C count; an element is
	// the sector come from or null, the event slot the .snd files filled, the next element). The
	// exe follows the actor at 0x4FF764 there, taken to be Rayman: in a recorded first visit
	// Learn_10's music changed when the hand-over moved him from the intro's sector into the woods'.
	void play_sector_sounds(app& a)
	{
		auto now = rayman_sector_now(a);
		if(!now || now == a.rayman_sector)
			return;
		std::optional<std::size_t> before = std::exchange(a.rayman_sector, now);
		std::uint32_t data = a.sound_sector_data[*now];
		if(!data || !a.sounds || !a.live->contains(data + 0x54, 12))
			return;
		std::uint32_t from = before ? a.sound_sector_super_objects[*before] : 0;
		std::uint32_t element = a.live->u32(data + 0x54);
		for(std::uint32_t n = a.live->u32(data + 0x5C); n && element && a.live->contains(element, 12); n--)
		{
			std::uint32_t wanted = a.live->u32(element);
			if(!wanted || wanted == from)
				a.sounds->request(a.live->u32(element + 4), 0, 1.0f, 0.0f, -1);
			element = a.live->u32(element + 8);
		}
	}

	// Custom bit 12 (0x800 in standard game +0x24): the actor isn't in view. The renderer sets it for
	// actors whose bounding sphere (super object +0x34: centre, radius at +0x0C) lies outside the
	// camera's side planes or behind its near plane, and clears it for the others (0x45C450, after
	// the brains, so scripts see the last frame's). Actors without a sphere count as in view.
	// Learn_10's fireflies fly off in front of the camera after inspecting Rayman and leave once
	// it is set; Rayman's shot skips its particles when it is.
	void update_visibility(app& a, const glm::mat4& view_projection)
	{
		if(!a.vm || !a.live)
			return;
		openrayman::game::live_memory& m = *a.live;
		// The frustum's planes from the matrix rows (left, right, bottom, top, near), pointing in.
		glm::mat4 t = glm::transpose(view_projection);
		const glm::vec4 planes[] = { t[3] + t[0], t[3] - t[0], t[3] + t[1], t[3] - t[1], t[3] + t[2] };
		std::vector<std::uint32_t> actors = a.vm->actors();
		for(std::uint32_t so : a.vm->generated().alive())
			actors.push_back(so);
		for(std::uint32_t so : actors)
		{
			std::uint32_t actor = a.vm->actor_of(so);
			std::uint32_t standard = actor && m.contains(actor + 4, 4) ? m.u32(actor + 4) : 0;
			if(!standard || !m.contains(standard + 0x24, 4))
				continue;
			std::uint32_t sphere = m.contains(so + 0x34, 4) ? m.u32(so + 0x34) : 0;
			auto placed = openrayman::game::global_placement(m, so);
			bool outside = false;
			if(sphere && placed && m.contains(sphere, 0x10))
			{
				vec3 centre = placed->apply({ m.f32(sphere), m.f32(sphere + 4), m.f32(sphere + 8) });
				float radius = m.f32(sphere + 0x0C) * placed->uniform_scale();
				for(const glm::vec4& plane : planes)
				{
					float length = glm::length(glm::vec3(plane));
					if(length > 0 && glm::dot(glm::vec3(plane), glm::vec3(centre[0], centre[1], centre[2])) + plane.w < -radius * length)
						outside = true;
				}
			}
			std::uint32_t bits = m.u32(standard + 0x24);
			m.write<std::uint32_t>(standard + 0x24, outside ? bits | 0x800u : bits & ~0x800u);
		}
	}

	// The same from the game camera, for ticks run before the first frame is drawn. The game draws
	// a frame after every tick, also behind its loading and near-black covers; a wake marks the
	// actors of a fresh sector out of view, and without a frame those that think only in view
	// (custom bit 0x1000: Learn_10's portal, node 77) stayed asleep.
	void update_game_camera_visibility(app& a)
	{
		if(!a.live || !a.camera_super_object || !a.window)
			return;
		auto placed = openrayman::game::global_placement(*a.live, a.camera_super_object);
		if(!placed)
			return;
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(a.window, &w, &h);
		glm::vec3 eye(placed->m[0][3], placed->m[1][3], placed->m[2][3]);
		glm::vec3 back(-placed->m[0][1], -placed->m[1][1], -placed->m[2][1]);
		glm::vec3 top(placed->m[0][2], placed->m[1][2], placed->m[2][2]);
		if(w <= 0 || h <= 0 || glm::length(back) < 1e-4f || glm::length(top) < 1e-4f)
			return;
		glm::mat4 projection = glm::perspective(0.9f, float(w) / float(h), 0.1f, 2000.0f);
		update_visibility(a, projection * glm::lookAt(eye, eye + glm::normalize(back), glm::normalize(top)));
	}

	// --place: Rayman at x, y, z turned by yaw degrees (at load, or at a later tick with @seconds,
	// e.g. after a level's opening scene has let go of him).
	void place_main_actor(app& a)
	{
		const auto& p = *a.start_place;
		float angle = p[3] * 0.01745329252f, c = std::cos(angle), s = std::sin(angle);
		game::write_transform(*a.live, a.vm->main_actor(), {p[0], p[1], p[2]}, {-s,c,0,-c,-s,0,0,0,1});
	}

	void run_ticks(app& a, double seconds)
	{
		if(!a.vm || !a.scripts)
			return;
		constexpr double tick = 1.0 / 60.0;
		a.script_clock += seconds;
		int ticks = 0;
		while(a.script_clock >= tick)
		{
			a.script_clock -= tick;
			// A slow frame runs at most a few ticks and lets the rest go, so the game slows down
			// instead of falling further behind. A long --time at load may run up to 10 minutes.
			if(++ticks > (a.screenshot || a.last_frame == 0 ? 36000 : 4))
			{
				a.script_clock = 0;
				break;
			}
			if(a.camera_super_object && !a.playing)
			{
				// Its -Y axis looks where ours does, Z as close to up as that allows.
				glm::vec3 y = -a.camera.forward();
				glm::vec3 x = glm::cross(y, glm::vec3(0, 0, 1));
				x = glm::length(x) > 1e-4f ? glm::normalize(x) : glm::vec3(1, 0, 0);
				glm::vec3 z = glm::cross(x, y);
				affine placed;
				for(int r = 0; r < 3; r++)
				{
					placed.m[r][0] = x[r];
					placed.m[r][1] = y[r];
					placed.m[r][2] = z[r];
					placed.m[r][3] = a.camera.position[r];
				}
				openrayman::game::set_global_placement(*a.live, a.camera_super_object, placed);
			}
			if(a.input)
			{
				if(!a.held_keys.empty())
				{
					std::array<bool, 256> down{};
					for(const app::held_key& h : a.held_keys)
						down[h.key] = down[h.key] || (a.tick >= h.from && a.tick < h.to);
					for(const app::held_key& h : a.held_keys)
						a.input->set_key(h.key, down[h.key]);
				}
				a.input->step();
			}
			a.tick++;
			// A --time run has no rendered frames between ticks. Refresh the same
			// visibility flags a frame would, including actors woken by a sector change.
			if(a.silent_run) update_game_camera_visibility(a);
			if(a.start_place && a.start_place_tick > 0 && a.tick == a.start_place_tick)
				place_main_actor(a);
			openrayman::game::step(*a.vm, a.thinkers, [](std::uint32_t self, const std::exception& e) {
				SDL_Log("Script error, actor 0x%08X: %s", self, e.what());
			});
			// Our choice (docs/IMPROVEMENTS.md): a level that opens with a cutscene starts with its bars already
			// in. The bars' actor (the one calling procedure 314; Fix's world actor) grows its var60 by 0.4
			// a millisecond towards 195 (var58, small bars) or 325.417 (var57, large) and clears the request
			// when it gets there, so setting var60 to the target finishes the slide at once.
			if(a.bars_in_place > 0)
			{
				a.bars_in_place--;
				if(const std::uint32_t bars = a.vm->view_bounds_actor)
				{
					const bool small = a.vm->get_variable(bars, 58).as_int() != 0;
					const bool large = a.vm->get_variable(bars, 57).as_int() != 0;
					if((small || large) && !a.vm->get_variable(bars, 59).as_int())
					{
						const float target = small ? 195.0f : 325.417f;
						a.vm->set_variable(bars, 60, openrayman::game::value::real(target));
						auto& view = a.vm->world_view_bounds();
						view.top = int(target * 0.23076923f);
						view.bottom = 1000 - int(target);
						a.bars_in_place = 0;
					}
				}
			}
			if((a.trace.active() || a.recording.rayman) && a.vm)
			{
				std::string held;
				if(a.input)
					for(int k = 1; k < 256; k++)
						if(a.input->key(std::uint8_t(k)))
							held += (held.empty() ? "" : ",") + openrayman::key_name(std::uint16_t(k));
				if(a.trace.active())
					a.trace.tick(*a.live, a.vm->main_actor(), a.vm->clock(), held);
				if(a.recording.rayman)
					a.recording.rayman->tick(*a.live, a.vm->main_actor(), a.vm->clock(), held);
			}
			if(std::exchange(a.vm->stop_sounds_requested, false))
			{
				if(a.sounds) a.sounds->stop_all();
				a.sound_sources.clear();
			}
			// A level change waits for the frame to finish (change_requested_level).
			if(a.vm->quit_requested || !a.vm->level_request.empty())
			{
				a.script_clock = 0;
				break;
			}
			// Screenshots and the --time run at load stay silent.
			for(const auto& request : a.vm->take_sounds())
				if(a.sounds && !a.screenshot && !a.silent_run)
				{
					if(request.procedure == 426 || request.procedure == 427)
					{
						a.sounds->pause_group(request.procedure == 426);
						continue;
					}
					// Procedure 115 and animations sound from their actor; 117..119 have no emitter
					// (0x4691E0 passes -1; 0x4520A0 centres them).
					if(request.procedure == 115 || request.procedure == 0)
					{
						auto [gain, pan] = sound_placement(a, request.source);
						// OPENRAYMAN_SOUND_LOG=<file>: each actor sound as it starts, its node, gain and pan.
						static std::ofstream sound_log = [] {
							const char* p = std::getenv("OPENRAYMAN_SOUND_LOG");
							return p && *p ? std::ofstream(p) : std::ofstream();
						}();
						if(sound_log.is_open() || a.recording.active())
						{
							auto node = std::ranges::find(a.super_object_at, request.source);
							auto placed = openrayman::game::global_placement(*a.live, request.source);
							float distance = placed ? glm::length(glm::vec3(placed->m[0][3], placed->m[1][3], placed->m[2][3]) - a.camera.position) : -1.0f;
							const std::string line = std::format("tick {} node {} event 0x{:08X} gain {:.3f} pan {:.2f} distance {:.1f} hears {}\n", a.tick,
								node == a.super_object_at.end() ? -1 : int(node - a.super_object_at.begin()), request.event, gain, pan, distance,
								camera_hears(a, request.source) ? 1 : 0);
							if(sound_log.is_open())
								sound_log << line << std::flush;
							if(a.recording.active())
								a.recording.sounds << line;
						}
						a.sounds->request(request.event, request.source, gain, pan, request.selector);
						a.sound_sources.insert(request.source);
					}
					else
						a.sounds->request(request.event, 0, 1.0f, 0.0f, request.selector);
				}
			if(!a.screenshot && !a.silent_run)
				play_sector_sounds(a);
			if(a.camera_log && a.playing && a.camera_super_object)
			{
				auto camera = openrayman::game::global_placement(*a.live, a.camera_super_object);
				auto player = openrayman::game::global_placement(*a.live, a.vm->main_actor());
				if(camera && player)
				{
					glm::vec3 c(camera->m[0][3], camera->m[1][3], camera->m[2][3]);
					glm::vec3 p(player->m[0][3], player->m[1][3], player->m[2][3]);
					float speed = a.camera_log_previous ? glm::length(c - *a.camera_log_previous) * 60.0f : 0.0f;
					a.camera_log << std::format("{},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f}\n",
						a.tick, p.x, p.y, p.z, c.x, c.y, c.z, glm::length(c - p), speed);
					a.camera_log_previous = c;
				}
			}
			else
				a.camera_log_previous.reset();
		}
		sync_generated(a);
		if(a.sounds)
			for(auto it = a.sound_sources.begin(); it != a.sound_sources.end();)
			{
				// An actor taken out of the world or hidden falls silent (the big-nosed flier buzzed on
				// after it flew off and put itself away). Hidden, not frozen: the ambient emitters
				// (Learn_10 nodes 31, 32) carry custom bit 14 and must keep playing.
				std::uint32_t source = *it;
				if(a.vm && (a.vm->removed(source) || (a.live->contains(source + 0x30, 4) && (a.live->u32(source + 0x30) & 0x2))))
				{
					a.sounds->stop_source(source);
					it = a.sound_sources.erase(it);
					continue;
				}
				// On leaving the active world, the exe stops every voice of the actor, including
				// one-shots already playing (0x40B410 -> 0x453040 -> 0x451DC0).
				if(a.vm && !a.vm->active(source))
				{
					a.sounds->stop_source(source, true);
					it = a.sound_sources.erase(it);
					continue;
				}
				auto [gain, pan] = sound_placement(a, source);
				a.sounds->place(source, gain, pan);
				// F10 recordings: every half second, each actor still sounding, as the sound log
				// numbers it, with its gain and distance now (loops started before the recording).
				// OPENRAYMAN_SOUNDING_LOG=<file> writes the same lines outside a recording.
				static std::ofstream sounding_log = [] {
					const char* p = std::getenv("OPENRAYMAN_SOUNDING_LOG");
					return p && *p ? std::ofstream(p) : std::ofstream();
				}();
				if((a.recording.active() || sounding_log.is_open()) && a.tick % 30 == 0)
				{
					auto node = std::ranges::find(a.super_object_at, source);
					auto placed = openrayman::game::global_placement(*a.live, source);
					float distance = placed ? glm::length(glm::vec3(placed->m[0][3], placed->m[1][3], placed->m[2][3]) - a.camera.position) : -1.0f;
					const std::string line = std::format("tick {} sounding node {} gain {:.3f} pan {:.2f} distance {:.1f} hears {} camera sector 0x{:X}\n", a.tick,
						node == a.super_object_at.end() ? -1 : int(node - a.super_object_at.begin()), gain, pan, distance, camera_hears(a, source) ? 1 : 0,
						a.camera_super_object && a.vm ? a.vm->actor_sector(a.camera_super_object) : 0u);
					if(a.recording.active())
						a.recording.sounds << line;
					if(sounding_log.is_open())
						sounding_log << line << std::flush;
				}
				++it;
			}
		// Where each actor is now: scripts move and turn them.
		for(std::size_t i = 0; i < a.actors.actors.size() && i < a.super_object_at.size(); i++)
			if(auto placed = openrayman::game::global_placement(*a.live, a.super_object_at[i]))
				a.actors.actors[i].world = *placed;
		// The current state is the first word after the initial state in the actor's 3D data.
		for(std::size_t i = 0; i < a.actors.actors.size() && i < a.actor_at.size(); i++)
		{
			actor_set::entry& actor = a.actors.actors[i];
			std::uint32_t data = a.live->u32(a.actor_at[i]);
			if(!data || i >= a.state_addresses.size())
				continue;
			std::uint32_t current = a.live->u32(data + 4);
			const auto& states = a.state_addresses[i];
			for(std::size_t k = 0; k < states.size() && k < actor.states.size(); k++)
			{
				if(states[k] == current)
				{
					// It loops if it names itself as what comes after; with nothing after it, it holds
					// its last frame (0x40E800 sets +0x8E and the animation stops updating).
					std::uint32_t after = a.live->u32(current + 0x20);
					actor.loops = after == current;
				}
				if(states[k] == current && actor.state != k)
				{
					actor.state = k;
					actor.animation = actor.states[k];
					a.state_since[i] = a.animation_time;
				}
			}
		}
	}

	// Poses every actor at the current animation time and hands the parts to the renderer.
	// Gives every live generated actor an entry to draw it with: one per pool slot, made the first
	// time the slot is used, showing its current object list.
	void sync_generated(app& a)
	{
		const auto& slots = a.vm->generated().slots();
		a.generated_entry.resize(slots.size());
		for(std::size_t k = 0; k < slots.size(); k++)
		{
			const auto& slot = slots[k];
			if(!slot.creator)
				continue;
			auto look = std::ranges::find(a.generated_looks, slot.model, &app::generated_look::model);
			if(look == a.generated_looks.end())
				continue;
			if(!a.generated_entry[k])
			{
				a.generated_entry[k] = a.actors.actors.size();
				a.actors.actors.push_back(look->entry);
				a.actor_at.push_back(0);
				a.super_object_at.push_back(0);
				a.state_addresses.emplace_back();
				a.state_since.push_back(a.animation_time);
			}
			std::size_t i = *a.generated_entry[k];
			actor_set::entry& entry = a.actors.actors[i];
			if(a.actor_at[i] != a.vm->actor_of(slot.super_object) || a.super_object_at[i] != slot.super_object)
			{
				entry = look->entry;
				a.actor_at[i] = a.vm->actor_of(slot.super_object);
				a.super_object_at[i] = slot.super_object;
				a.state_addresses[i] = look->state_addresses;
			}
			// What its parts look like: the object list procedure 94 picked (3D data +0x10), else its own.
			std::uint32_t data = a.live->u32(a.actor_at[i]);
			std::uint32_t list = data ? a.live->u32(data + 0x10) : 0;
			if(!look->lists.contains(list))
				list = data ? a.live->u32(data + 0x0C) : 0;
			if(auto found = look->lists.find(list); found != look->lists.end())
			{
				entry.meshes = found->second.first;
				entry.scales = found->second.second;
			}
		}
	}

	// Taken out of the world by its scripts (procedure 139), or hidden: super object flag 0x2 (+0x30),
	// which scripts set with procedure 312. Learn_10's tutorial controller (node 69) hides the
	// Rayman-shaped double (node 71) and the caged Lum (node 70) this way until the tutorial
	// starts, and node 45 the end cutscene's actors; we drew them all.
	bool hidden(const app& a, std::uint32_t super_object)
	{
		if(a.vm->removed(super_object))
			return true;
		if(a.live->contains(super_object + 0x30, 4) && (a.live->u32(super_object + 0x30) & 0x2))
			return true;
		// World actor visibility (0x45C450) requires standard +0x29 bit 4, maintained by
		// the wake/sleep pass. Screen objects use the separate 0x45CC80/0x45CA30 pass.
		return !a.vm->screen_objects().contains(super_object) && !a.vm->active(super_object);
	}

	void update_actors(app& a)
	{
		a.actor_draw_trace.clear();
		const bool trace_draw = std::getenv("OPENRAYMAN_DRAW_TRACE") != nullptr || a.recording.active();
		auto trace_actor = [&](std::size_t i, const char* result, double frame, std::size_t count) {
			if(!trace_draw || !a.vm || i >= a.actor_at.size() || i >= a.super_object_at.size()) return;
			const auto so = a.super_object_at[i], actor = a.actor_at[i];
			const auto data = a.live->u32(actor), standard = a.live->u32(actor + 4);
			const auto& entry = a.actors.actors[i];
			a.actor_draw_trace += std::format("node={} result={} active={} hidden={} soflags={:08X} standard={:02X} custom={:08X} state={:08X} cached_state={} anim={} frame={:.3f} live_frame={} parts={} blend={:.2f}:{}@{:.1f} pos={:.2f},{:.2f},{:.2f}\n",
				entry.node, result, a.vm->active(so), hidden(a, so), a.live->u32(so + 0x30),
				standard ? a.live->u8(standard + 0x29) : 0, standard ? a.live->u32(standard + 0x24) : 0,
				data ? a.live->u32(data + 4) : 0, entry.state ? int(*entry.state) : -1, entry.animation ? int(*entry.animation) : -1,
				frame, data ? a.live->read<std::uint16_t>(data + game::data_3d::frame) : 0, count, entry.blend, entry.blend_animation, entry.blend_frame,
				entry.world.m[0][3], entry.world.m[1][3], entry.world.m[2][3]);
		};
		std::vector<posed_part> parts;
		std::vector<bool> screen_parts; // per part: drawn by the exe's screen pass (procedure 409)
		std::vector<float> part_alpha;  // per part: its actor's transparency level
		std::vector<std::uint8_t> part_colour_set;
		for(std::size_t i = 0; i < a.actors.actors.size(); i++)
		{
			actor_set::entry& actor = a.actors.actors[i];
			// Taken out of the world by its scripts.
			if(a.vm && a.scripts && i < a.super_object_at.size() && hidden(a, a.super_object_at[i]))
			{
				trace_actor(i, "hidden", 0, 0);
				a.loop_watches.erase(i); // what it showed before is stale once it shows again (card 97)
				continue;
			}
			if(a.vm && a.scripts && i < a.selected_object_lists.size() && i < a.actor_at.size())
			{
				// Procedure 94 (exe 0x468730, case 0x5E) selects the list at 3D data +0x10.
				// Jail_20 node 9 replaces the saved red laser meshes with blue ones at startup.
				std::uint32_t data = a.live->u32(a.actor_at[i]);
				std::uint32_t list = data ? a.live->u32(data + 0x10) : 0;
				if(list != a.selected_object_lists[i])
					if(auto found = a.scene_object_lists.find(list); found != a.scene_object_lists.end())
					{
						actor.meshes = found->second.first;
						actor.scales = found->second.second;
						a.selected_object_lists[i] = list;
					}
			}
			double frame = 0;
			if(actor.animation && *actor.animation < a.bank.animations.size())
			{
				// The animation's speed is in frames per second (mostly 30, some 25 or 60). Each actor's
				// clock starts when its current state began.
				std::uint16_t speed = a.bank.animations[*actor.animation].speed;
				double since = i < a.state_since.size() ? a.state_since[i] : 0.0;
				frame = (a.animation_time - since) * (speed ? speed : 30);
				if(a.vm && a.scripts && i < a.actor_at.size())
				{
					// With the scripts on, the game's animation player decides: its time into the current
					// animation (3D data +0xC0, ms), plus how far we are towards the next tick, at the
					// rate it plays (3D data +0xE0, from the state, procedure 89).
					std::uint32_t data = a.live->u32(a.actor_at[i]);
					if(data)
					{
						std::uint8_t rate = a.live->u8(data + openrayman::game::data_3d::playback_rate);
						frame = (a.live->u32(data + 0xC0) + a.script_clock * 1000.0) * (rate ? rate : (speed ? speed : 30)) / 1000.0;
						// 0x40E800 holds the last frame for the script pass, including
						// self-looping states. Extrapolating here wrapped the fish early.
						if(a.live->u8(data + openrayman::game::data_3d::reached_end))
							frame = a.live->read<std::uint16_t>(data + openrayman::game::data_3d::frame);
					}
					// A state that moves on to another holds its last frame until the next tick switches.
					std::uint16_t frames = a.bank.animations[*actor.animation].frames;
					if(!actor.loops && frames)
						frame = std::min(frame, double(frames - 1));
					// Our smoothing (docs/IMPROVEMENTS.md), over up to 4 frames after a switch, out of the
					// pose drawn just before it: when a loop really started over (same state, the frame
					// jumped back), or after a state change whose new pose starts close to the old one
					// (a small mismatch in the data: the Jail_20 cell wake-up, card 100). Bigger changes
					// are meant as cuts and snap, as in the game.
					constexpr float close_enough = 0.25f; // units, the largest part move to smooth over
					auto& watch = a.loop_watches[i];
					const double window = std::min(4.0, frames / 4.0);
					auto last_drawn = [&](std::uint16_t from) {
						return std::min(watch.frame, double(std::max<int>(a.bank.animations[from].frames - 1, 0)));
					};
					auto start_blend = [&](std::uint16_t from) {
						watch.blending = true;
						watch.from_animation = from;
						watch.from_frame = last_drawn(from);
						watch.start = frame;
					};
					if(watch.state != actor.state)
					{
						watch.blending = false;
						if(watch.state && watch.animation && *watch.animation < a.bank.animations.size() && window > 0 &&
							openrayman::pose_gap(a.bank, *watch.animation, last_drawn(*watch.animation), *actor.animation, frame) <= close_enough)
							start_blend(*watch.animation);
					}
					else if(actor.loops && frames > 1 && frame + frames / 2.0 < watch.frame)
						start_blend(*actor.animation);
					const double into = frame - watch.start;
					if(watch.blending && window > 0 && into >= 0 && into < window)
					{
						float s = float(into / window);
						actor.blend = 1 - s * s * (3 - 2 * s);
						actor.blend_animation = watch.from_animation;
						actor.blend_frame = watch.from_frame;
					}
					else
					{
						watch.blending = false;
						actor.blend = 0;
					}
					watch.state = actor.state;
					watch.animation = actor.animation;
					watch.frame = frame;
				}
			}
			std::size_t first_part = parts.size();
			try
			{
				const channel_positions empty;
				const auto& positions = a.vm && a.scripts && i < a.super_object_at.size()
					? a.vm->placed_channels(a.super_object_at[i]) : empty;
				pose_actor(actor, a.bank, frame, parts, positions);
			}
			catch(const std::exception&)
			{
				// Bad animation data: leave this actor out rather than stop the level.
			}
			trace_actor(i, "submitted", frame, parts.size() - first_part);
			if(a.vm && a.scripts && i < a.super_object_at.size() && a.vm->screen_objects().contains(a.super_object_at[i]))
				screen_parts.insert(screen_parts.end(), parts.size() - std::min(parts.size(), first_part), true);
			screen_parts.resize(parts.size(), false);
			// Its transparency level counts while draw flag 8 is clear (procedures 11 and 12, 0x472450):
			// super object +0x2C, level 0..255 at +0x38.
			float alpha = 1;
			if(a.vm && a.scripts && i < a.super_object_at.size())
				if(std::uint32_t so = a.super_object_at[i]; so && a.live->contains(so + 0x2C, 0x10) && !(a.live->u32(so + 0x2C) & 8))
					alpha = std::clamp(a.live->f32(so + 0x38) / 255.0f, 0.0f, 1.0f);
			part_alpha.resize(parts.size(), alpha);
			std::uint8_t selected = 0;
			if(a.vm && a.scripts && i < a.actor_at.size() && a.actor_at[i] && a.live->contains(a.actor_at[i] + 0x1C, 4))
			{
				std::uint32_t state = a.live->u32(a.actor_at[i] + 0x1C);
				if(state && a.live->contains(state + 0x38, 2) && a.live->u8(state + 0x38) == 1)
					selected = a.live->u8(state + 0x39);
			}
			part_colour_set.resize(parts.size(), selected);
		}
		std::vector<level_renderer::part> out;
		out.reserve(parts.size());
		for(std::size_t n = 0; n < parts.size(); n++)
		{
			const posed_part& part = parts[n];
			glm::mat4 model(1.0f);
			for(int r = 0; r < 3; r++)
			{
				for(int c = 0; c < 4; c++)
					model[c][r] = part.world.m[r][c];
			}
			out.push_back({ part.mesh, model, {}, part.morph_mesh, part.morph_amount, n < screen_parts.size() && screen_parts[n], n < part_alpha.size() ? part_alpha[n] : 1.0f, part_colour_set[n] });
		}
		a.level->set_parts(std::move(out));
		a.level->set_time(a.animation_time);

		std::vector<level_renderer::shadow> shadows;
		for(std::size_t i = 0; i < a.actors.actors.size(); i++)
		{
			const actor_set::entry& actor = a.actors.actors[i];
			if(a.vm && a.scripts && i < a.super_object_at.size() && hidden(a, a.super_object_at[i]))
				continue;
			// Actors with nothing to show (set up by scripts later) don't get a shadow yet either.
			bool visible = std::ranges::any_of(actor.meshes, [](const auto& mesh) { return mesh.has_value(); });
			// Every shadow is drawn, also those the game's scripts leave off (like the Globox kids'): a
			// deliberate improvement. The game's own switch is in actor.shadow->shown.
			if(!actor.shadow || !visible)
				continue;
			glm::mat4 model(1.0f);
			for(int r = 0; r < 3; r++)
			{
				for(int c = 0; c < 4; c++)
					model[c][r] = actor.world.m[r][c];
			}
			shadows.push_back({ model, glm::vec2(actor.shadow->width, actor.shadow->length) });
		}
		a.level->set_shadows(std::move(shadows));

		// The particles the scripts started, where they show now.
		std::vector<level_renderer::particle> particles;
		if(a.vm && a.scripts)
		{
			const openrayman::game::particles& all = a.vm->particles();
			double now = a.vm->clock() / 1000.0;
			for(const auto& p : all.all())
			{
				auto at = all.shown_at(p, now);
				// 0x460A10: flag 0x10000000 lies flat on the ground; 0x8000000 stands on its point.
				const bool flat = (p.flags & 0xF0000000) == 0x10000000;
				const float lift = !flat && (p.flags & 0x8000000) ? p.height : 0.0f;
				particles.push_back({ glm::vec3(at[0], at[1], at[2] + lift), p.size, std::min(p.alpha, 255.0f) / 255.0f, p.material,
					p.flags ? p.height : -1.0f, flat, float(now - p.born) });
			}
		}
		a.level->set_particles(std::move(particles));
	}

	// Debug: puts the actor you're looking at (the one nearest the middle of the view, within 150
	// units) into the next or previous state of its family; with `whole_family`, every actor of
	// that family. Saved states are often placeholders the game's scripts replace, so this shows
	// the animations the game really plays.
	void step_actor_state(app& a, int delta, bool whole_family)
	{
		const glm::vec3 eye = a.camera.position, forward = a.camera.forward();
		actor_set::entry* best = nullptr;
		float best_angle = 0;
		for(actor_set::entry& actor : a.actors.actors)
		{
			if(actor.states.size() < 2)
				continue;
			glm::vec3 to(actor.world.m[0][3] - eye.x, actor.world.m[1][3] - eye.y, actor.world.m[2][3] - eye.z);
			float distance = glm::length(to);
			if(distance < 1e-3f || distance > 150)
				continue;
			float angle = std::acos(std::clamp(glm::dot(to / distance, forward), -1.0f, 1.0f));
			if(angle < glm::radians(30.0f) && (!best || angle < best_angle))
			{
				best = &actor;
				best_angle = angle;
			}
		}
		if(!best)
		{
			a.actor_note = "no actor with states in view";
			update_title(a);
			return;
		}
		const long count = static_cast<long>(best->states.size());
		const std::size_t state = static_cast<std::size_t>((static_cast<long>(best->state.value_or(0)) + delta % count + count) % count);
		const auto family = best->family;
		const std::size_t node = best->node;
		std::size_t changed = 0;
		for(actor_set::entry& actor : a.actors.actors)
		{
			if(&actor != best && !(whole_family && actor.family == family))
				continue;
			if(state >= actor.states.size())
				continue;
			actor.state = state;
			actor.animation = actor.states[state];
			changed++;
		}
		a.actor_note = std::format("node {} state {}/{}", node, state, count);
		if(best->animation)
			a.actor_note += std::format(" (animation {})", *best->animation);
		else
			a.actor_note += " (no animation)";
		if(whole_family)
			a.actor_note += std::format(", {} actors", changed);
		SDL_Log("%s", a.actor_note.c_str());
		update_title(a);
	}

	void load_level(app& a, std::size_t index)
	{
		const auto& levels = a.game->game_dsb().levels;
		if(levels.empty())
			return;
		a.level_index = index;
		a.level_error.clear();
		a.level_stats = {};
		a.actor_note.clear();
		try
		{
			a.dialog_overlay.reset();
			a.dialog_memory.reset();
			Uint64 start = SDL_GetTicks();
			if(openrayman::game::only_new_game(levels[index]))
				a.new_game = true; // the pirate ship has no revisit (first_visit.h)
			level_files files = load_level_files(*a.game, levels[index]);
			level_header header = read_level_header(files);
			scene tree(files.memory, header);
			std::vector<placed_geometry> geometry = static_geometry(files.memory, tree);
			// Actors (platforms, Lums, characters...): their meshes, placed every frame.
			a.bank = read_animation_bank(files);
			a.actors = read_actors(files.memory, tree, a.bank);
			// The game's lights: each piece of geometry takes those of its sector.
			level_lights lights = read_lights(files.memory, tree);
			std::vector<std::optional<std::size_t>> sectors;
			for(const placed_geometry& placed : geometry)
				sectors.push_back(lights.sector_of(tree, placed.node));
			a.sound_sectors = lights;
			// Every shadow in the game uses the same blob material, from Fix.
			const visual_material* shadow_material = nullptr;
			for(const actor_set::entry& actor : a.actors.actors)
			{
				if(actor.shadow && !shadow_material)
					shadow_material = &actor.shadow->material;
			}
			// The templates scripts copy while the game runs (the actor named right after function 66),
			// with every object list their scripts may switch to (procedure 94): their meshes have to be
			// in the set before the renderer takes it.
			level_brains brains = read_brains(files.memory, tree);
			a.generated_looks.clear();
			a.generated_entry.clear();
			{
				std::vector<memory_location> models;
				auto find_models = [&](const script& sc) {
					for(std::size_t k = 0; k + 1 < sc.nodes.size(); k++)
					{
						const script_node& n = sc.nodes[k];
						if(n.type == std::uint8_t(node_type::function) && n.param == 66 && sc.nodes[k + 1].target
							&& std::ranges::find(models, *sc.nodes[k + 1].target) == models.end())
							models.push_back(*sc.nodes[k + 1].target);
					}
				};
				auto each_script = [](const ai_model& model, const auto& f) {
					for(const auto& list : { &model.behaviours, &model.reflexes })
						for(const behaviour& b : *list)
						{
							for(const script& sc : b.rules)
								f(sc);
							if(b.schedule)
								f(*b.schedule);
						}
					for(const macro& m : model.macros)
						f(m.body);
				};
				for(const auto& [key, model] : brains.models)
					each_script(model, find_models);
				// Placed actors can change object lists too, not only generated templates.
				a.scene_object_lists.clear();
				a.selected_object_lists.assign(a.actors.actors.size(), 0);
				auto cache_list = [&](const memory_location& list) {
					if(a.scene_object_lists.contains(list.address()))
						return;
					auto& [meshes, scales] = a.scene_object_lists[list.address()];
					add_meshes(a.actors, files.memory, read_object_list(files.memory, list), meshes, scales);
				};
				for(const auto& actor : a.actors.actors)
					if(auto data = files.memory.follow(*tree.node(actor.node).data))
						for(std::uint32_t off : { 0x0Cu, 0x10u })
							if(auto list = files.memory.follow(*data + off))
								cache_list(*list);
				for(const auto& [key, model] : brains.models)
					each_script(model, [&](const script& sc) {
						for(const script_node& n : sc.nodes)
							if(n.type == std::uint8_t(node_type::reference_31) && n.target)
								cache_list(*n.target);
					});
				for(const memory_location& at : models)
				{
					try
					{
						actor t = read_actor_at(files.memory, a.bank, at);
						app::generated_look look;
						look.model = at.address();
						look.entry.animation = t.animation;
						look.entry.family = t.family;
						look.entry.states = t.states;
						look.entry.state_at = t.state_at;
						// Spawning happens after load_level returns and destroys files. Never resolve
						// these locations at spawn time: their block pointers will be dangling.
						for(const memory_location& state : t.state_at)
							look.state_addresses.push_back(state.address());
						look.entry.state = t.state;
						look.entry.shadow = t.shadow;
						std::vector<memory_location> lists;
						if(auto data = files.memory.follow(at))
							for(std::uint32_t off : { 0x0Cu, 0x10u })
								if(auto list = files.memory.follow(*data + off))
									lists.push_back(*list);
						level_brains own = read_brain_at(files.memory, at);
						for(const auto& [k, model] : own.models)
							each_script(model, [&](const script& sc) {
								for(const script_node& n : sc.nodes)
									if(n.type == std::uint8_t(node_type::reference_31) && n.target)
										lists.push_back(*n.target);
							});
						// Its scripts' particle materials are loaded with the rest (the shot's bounce
						// sparkles), so its model joins the level's.
						brains.models.merge(own.models);
						for(const memory_location& list : lists)
						{
							if(look.lists.contains(list.address()))
								continue;
							auto& [meshes, scales] = look.lists[list.address()];
							add_meshes(a.actors, files.memory, read_object_list(files.memory, list), meshes, scales);
						}
						a.generated_looks.push_back(std::move(look));
					}
					catch(const std::exception& e)
					{
						SDL_Log("Template actor at 0x%08X left out: %s", at.address(), e.what());
					}
				}
			}
			a.level_stats = a.level->load(geometry, a.actors.meshes, a.archives, std::move(lights), sectors, shadow_material);
			a.level->set_backdrop(levels[index]);

			// The materials scripts draw particles with (procedure 503's game materials).
			std::map<std::uint32_t, visual_material> effect_materials;
			for(const auto& [key, model] : brains.models)
			{
				auto take = [&](const script& sc) {
					for(const script_node& n : sc.nodes)
					{
						if(n.type != std::uint8_t(node_type::game_material) || !n.target)
							continue;
						if(auto visual = files.memory.follow(*n.target))
							effect_materials.try_emplace(n.target->address(), read_visual_material(files.memory, *visual));
					}
				};
				for(const auto& list : { &model.behaviours, &model.reflexes })
					for(const behaviour& b : *list)
					{
						for(const script& sc : b.rules)
							take(sc);
						if(b.schedule)
							take(*b.schedule);
					}
				for(const macro& m : model.macros)
					take(m.body);
			}
			a.level->load_effect_materials(effect_materials, a.archives);

			// What the level collides with, for the C overlay: one colour per surface kind (the
			// lowest flag bit; grey for plain surfaces). Green (0x0004) is grabbable, blue (0x4000) water.
			std::vector<openrayman::level_renderer::collision_face> faces;
			std::vector<openrayman::collision_triangle> collision = openrayman::static_collision(files.memory, tree);
			for(const openrayman::collision_triangle& t : collision)
			{
				static const glm::vec3 palette[16] = { { 1.0f, 0.55f, 0.1f }, { 1.0f, 0.2f, 0.8f }, { 0.2f, 0.9f, 0.2f }, { 0.6f, 0.3f, 1.0f },
					{ 1.0f, 1.0f, 0.2f }, { 0.2f, 1.0f, 1.0f }, { 0.6f, 1.0f, 0.4f }, { 1.0f, 0.4f, 0.4f },
					{ 1.0f, 0.1f, 0.1f }, { 0.9f, 0.6f, 0.9f }, { 1.0f, 0.5f, 0.0f }, { 0.5f, 0.0f, 0.0f },
					{ 0.7f, 0.7f, 0.2f }, { 0.4f, 0.6f, 0.4f }, { 0.2f, 0.4f, 1.0f }, { 0.9f, 0.9f, 0.9f } };
				glm::vec3 colour(0.55f, 0.6f, 0.7f);
				if(t.flags)
					colour = palette[std::countr_zero(unsigned(t.flags))];
				openrayman::level_renderer::collision_face f;
				for(int i = 0; i < 3; i++)
					f.corners[i] = { t.corners[i][0], t.corners[i][1], t.corners[i][2] };
				f.normal = { t.normal[0], t.normal[1], t.normal[2] };
				f.colour = glm::vec4(colour, 0.45f);
				faces.push_back(f);
			}
			a.level->set_collision(std::move(faces));
			a.level->set_show_collision(a.show_collision);

			// The level selects its player in GPT; hubs can use an actor other than Fix Rayman.
			a.live = std::make_unique<openrayman::game::live_memory>(files.memory);
			std::uint32_t main_actor = header.main_actor ? header.main_actor->address() : 0;
			a.vm = std::make_unique<openrayman::game::interpreter>(*a.live, main_actor);
			openrayman::game::register_handlers(*a.vm);
			if(a.carried_entry) a.vm->menu_request = *std::exchange(a.carried_entry, {});
			openrayman::game::register_dialog_handlers(*a.vm);
			auto saved_language = openrayman::game::read_language(openrayman::game::save_root());
			openrayman::game::set_dialog_resolver(*a.vm, files, saved_language.value_or(0));
			a.vm->on_language_selected = [](unsigned language) {
				if(!openrayman::game::write_language(openrayman::game::save_root(), language))
					SDL_Log("Could not save the language selection");
			};
			// Procedure 369: a script asks for a string to be opened (the Menu level's Site entry).
			// The scripts still name the dead 1999 address; we open the project page instead, and
			// say so, because a browser opening by itself can look like something went wrong.
			a.vm->on_open_url = [](const std::string& url) {
				SDL_Log("Opening %s", url.c_str());
				if(!SDL_OpenURL(url.c_str()))
					SDL_Log("Could not open %s: %s", url.c_str(), SDL_GetError());
			};
			a.vm->save_folder = openrayman::game::save_root().string();
			a.vm->set_world(openrayman::game::world_collision(std::move(collision)));
			a.vm->set_animations(&a.bank);
			// The level's animation events, then the sound events the scripts and animations name
			// (Fix.snd and the level's .snd).
			openrayman::game::install_level_events(*a.live, a.bank);
			openrayman::game::apply_sound_slots(*a.live, *a.game, levels[index]);
			if(a.sounds)
				a.sounds->stop_all();
			if(a.sounds && !a.screenshot && header.startup_sound)
				a.sounds->request(*header.startup_sound, 0, 1.0f, 0.0f, -1);
			a.sound_sources.clear();
			const bool learn_opening = !a.save_slot && a.new_game &&
				openrayman::game::learn_10_opening(levels[index]) &&
				(!a.carried_story || !openrayman::game::story_has(*a.carried_story, 1091));
			a.bars_in_place = 120; // any level that opens with a cutscene (Learn_10, Bast_20, ...)
			// The story so far (first_visit.h): a revisit includes the completed intro and Learn_10; a new
			// game has the pirate ship and the intro behind it.
			// Explicit story-bit overrides: 1095 gives
			// Rayman the silver (blue) shot, 1143 the golden one that charges.
			{
				std::vector<std::uint32_t> fix_actors;
				for(const fix_actor_placement& placed : header.placements)
					fix_actors.push_back(placed.super_object.address());
				// The menu starts from Fix's own state, as the game boots: no save is loaded yet.
				bool menu = std::ranges::equal(levels[index], std::string_view("Menu"),
					[](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
				// A level reached from another keeps the story the scripts left. Learn_10's
				// first visit runs its director, which sets bit 1091 at the hand-over.
				if(a.carried_story)
					a.vm->set_story_words(fix_actors, *a.carried_story);
				if(menu && saved_language) a.vm->set_story_bit(fix_actors, 1006);
				if(!menu && !a.save_slot && !a.carried_story)
					for(std::uint32_t bit : (learn_opening || (a.intro_start && !openrayman::game::only_new_game(levels[index]))) ? std::span<const std::uint32_t>(openrayman::game::intro_story_bits) : openrayman::game::start_story_bits(levels[index], a.new_game))
						a.vm->set_story_bit(fix_actors, bit);
				a.carried_story.reset();
				a.fix_actors = fix_actors;
				for(std::int32_t bit : a.story_bits)
					if(!a.save_slot) a.vm->set_story_bit(fix_actors, bit);
			}
			a.input.reset();
			a.tick = 0;
			a.trace.restart(levels[index]);
			if(a.recording.rayman)
				a.recording.rayman->restart(levels[index]);
			try
			{
				a.input = std::make_unique<openrayman::game::input>(*a.live, read_input_actions(files));
			}
			catch(const std::exception& e)
			{
				SDL_Log("No input actions: %s", e.what());
			}
			a.thinkers.clear();
			for(std::size_t n = 0; n < tree.nodes().size(); n++)
			{
				if(tree.node(n).type == super_object_type::actor)
					a.thinkers.push_back(tree.node(n).at.address());
			}
			a.vm->set_actors(a.thinkers);
			a.registry = read_save_registry(files.memory, header, tree);
			if(a.carried_general)
			{
				openrayman::game::apply_save_entries(*a.vm, tree, a.registry.general, *a.carried_general, a.registry.general_size);
				a.carried_general.reset();
				if(auto saved = a.level_progress.find(index); saved != a.level_progress.end())
					openrayman::game::apply_save_entries(*a.vm, tree, a.registry.level, saved->second, a.registry.level_size);
			}
			else if(a.save_slot)
			{
				SDL_Log("Save slot %u from %s", a.save_slot, openrayman::game::save_root().string().c_str());
				openrayman::game::load_save_slot(*a.vm, openrayman::game::save_root(), files, header, tree, a.save_slot);
			}
			else if(!a.new_game)
				openrayman::game::apply_revisit_state(*a.vm, tree, levels[index]);
			if(a.save_slot)
				for(std::int32_t bit : a.story_bits) a.vm->set_story_bit(a.fix_actors, bit);
			a.camera_super_object = 0;
			for(const fix_actor_placement& placed : header.placements)
			{
				if(a.live->contains(placed.actor.address() + 0x10, 4) && a.live->u32(placed.actor.address() + 0x10))
					a.camera_super_object = placed.super_object.address();
			}
			a.actor_at.clear();
			a.super_object_at.clear();
			a.state_addresses.clear();
			for(const actor_set::entry& actor : a.actors.actors)
			{
				// Where it is: the loader's job for Fix's actors (their placement is in the level header).
				if(auto saved = openrayman::game::global_placement(*a.live, tree.node(actor.node).at.address()))
				{
					float d = 0;
					for(int r = 0; r < 3; r++)
						for(int c = 0; c < 4; c++)
							d = std::max(d, std::abs(saved->m[r][c] - actor.world.m[r][c]));
					if(d > 1e-3f)
						openrayman::game::set_global_placement(*a.live, tree.node(actor.node).at.address(), actor.world);
				}
				auto& states = a.state_addresses.emplace_back();
				for(const memory_location& at : actor.state_at)
					states.push_back(at.address());
				a.actor_at.push_back(tree.node(actor.node).data ? tree.node(actor.node).data->address() : 0);
				a.super_object_at.push_back(tree.node(actor.node).at.address());
			}
			a.state_since.assign(a.actors.actors.size(), 0.0);
			a.sound_sector_super_objects.clear();
			a.sound_sector_data.clear();
			for(const sector& s : a.sound_sectors.sectors)
			{
				a.sound_sector_super_objects.push_back(tree.node(s.node).at.address());
				a.sound_sector_data.push_back(tree.node(s.node).data ? tree.node(s.node).data->address() : 0);
			}
			// Where Rayman stands before the hand-over: his saved place (the intro's), so moving him
			// to the hand-over plays what the game plays there.
			a.rayman_sector = rayman_sector_now(a);
			// Levels without an implemented intro use the hand-over shortcut (first_visit.h).
			if(a.new_game && !a.intro_start && !learn_opening && !a.save_slot && !header.placements.empty())
				openrayman::game::apply_first_visit_hand_over(*a.live, *a.vm, tree, levels[index], header.placements[0].super_object.address(), a.camera_super_object);
			a.script_clock = 0;
			a.animation_time = a.start_time.value_or(0.0);
			a.start_time.reset();
			for(const auto& [node, state] : a.start_states)
			{
				for(actor_set::entry& actor : a.actors.actors)
				{
					if(actor.node == node && state < actor.states.size())
					{
						actor.state = state;
						actor.animation = actor.states[state];
					}
				}
			}
			// --behaviour: start an actor's scripts in another normal behaviour, to check parts that wait
			// on what the engine can't do yet (the game's camera, Rayman).
			for(const auto& [node, which] : a.start_behaviours)
			{
				if(node >= tree.nodes().size())
					continue;
				openrayman::game::live_memory& m = *a.live;
				std::uint32_t actor = tree.node(node).data ? tree.node(node).data->address() : 0;
				std::uint32_t brain = actor && m.contains(actor + 0xC, 4) ? m.u32(actor + 0xC) : 0;
				std::uint32_t mind = brain && m.contains(brain, 4) ? m.u32(brain) : 0;
				std::uint32_t model = mind ? m.u32(mind) : 0;
				// The model's behaviour list: a pointer to the array, then its u8 count.
				std::uint32_t list = model ? m.u32(model) : 0;
				if(!list || which >= m.u8(list + 4))
					continue;
				a.vm->change_behaviour(m.u32(mind + 4), m.u32(list) + std::uint32_t(which) * 0xC, false);
			}
			a.start_behaviours.clear();
			a.start_states.clear();
			// Start where the game does. The first two Fix actors look like Rayman and the game camera
			// (placed above and behind him), so look from the camera at Rayman. Levels without them
			// start in the middle, looking along +X.
			auto position = [&](std::size_t i) {
				const auto& p = header.placements[i].matrix.position;
				return glm::vec3(p[0], p[1], p[2]);
			};
			a.camera = fly_camera{};
			a.camera.position = (a.level_stats.min + a.level_stats.max) * 0.5f;
			if(header.placements.size() >= 2 && position(0) != glm::vec3(0) && position(1) != position(0))
			{
				a.camera.position = position(1);
				glm::vec3 to = glm::normalize(position(0) - position(1));
				a.camera.yaw = std::atan2(to.y, to.x);
				a.camera.pitch = std::asin(to.z);
			}
			if(a.start_camera)
			{
				a.camera = *a.start_camera;
				a.start_camera.reset();
			}
			// Without --camera a game starts as the game does: the keyboard to Rayman and the view from
			// the game's camera, which starts where the level puts it (so its arrival move shows).
			// With --camera we fly from there; Tab switches either way.
			else if(a.input && a.camera_super_object && !a.chose_view)
			{
				a.playing = true;
				a.actor_note = "playing: keys go to the game, Tab to fly";
			}
			a.chose_view = true;
			if(a.start_place && a.vm && a.start_place_tick == 0)
				place_main_actor(a);
			// --time: let the scripts run up to then, so screenshots show what they picked.
			a.silent_run = true;
			run_scripts(a, a.animation_time);
			a.silent_run = false;
			// The startup script requests a cut on its first tick; the camera lands on the second.
			// Both run before the first frame. A revisit takes longer: node 4 puts Rayman by the portal
			// on the first tick, at full size beside the new-game spot, and the portal (node 77) takes
			// him and shrinks him into itself (procedure 173) a few ticks later. The game hides this
			// behind loading and node 4's near-black cover; ours runs on until he is shrunk, up to a
			// second for a level without a portal. A save start (--save) arrives the same way. Each
			// tick sees what the game camera sees, as a drawn frame would (the portal thinks only in view).
			constexpr long settle_ticks = 4;
			if(a.playing && a.camera_super_object && a.vm)
				while(a.tick < 60 && a.scripts)
				{
					if(a.tick >= settle_ticks)
					{
						auto p = openrayman::game::global_placement(*a.live, a.vm->main_actor());
						if(a.new_game || !p || p->uniform_scale() < 0.5f)
							break;
					}
					update_game_camera_visibility(a);
					run_scripts(a, 1.0 / 60.0);
				}
			update_actors(a);
			a.screen_font_actor = game::screen_slot_font_actor(files);
			a.dialog_memory = std::make_unique<memory_image>(std::move(files.memory));
			a.dialog_overlay = std::make_unique<dialog_renderer>(*a.dialog_memory, a.archives, a.api);
			SDL_Log("%s: %zu meshes, %zu triangles, %zu textures (%zu missing), %llu ms", levels[index].c_str(),
				a.level_stats.meshes, a.level_stats.triangles, a.level_stats.textures, a.level_stats.missing_textures,
				static_cast<unsigned long long>(SDL_GetTicks() - start));

		}
		catch(const std::exception& e)
		{
			a.level_error = e.what();
			a.bank = {};
			a.actors = {};
			a.level->load({}, {}, a.archives);
			SDL_Log("Could not load %s: %s", levels[index].c_str(), e.what());
		}
		update_title(a);
	}

	void step_level(app& a, long delta)
	{
		if(!a.game || a.game->game_dsb().levels.empty())
			return;
		long count = static_cast<long>(a.game->game_dsb().levels.size());
		load_level(a, static_cast<std::size_t>((static_cast<long>(a.level_index) + delta % count + count) % count));
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

			a.sounds.reset();
			a.game = std::move(game);
			a.sounds = std::make_unique<sound_player>(*a.game);
			a.archives = std::move(archives);
			a.textures = std::move(textures);
			a.music_files = a.game->music_files();
			a.music_index.reset();
			if(a.music)
				a.music->stop();
			if(!a.screenshot) save_folder(folder);
			show_texture(a, 0);

			const auto& levels = a.game->game_dsb().levels;
			auto start = std::ranges::find_if(levels, [&](const std::string& name) {
				return std::ranges::equal(name, a.start_level, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
			});
			load_level(a, start == levels.end() ? 0 : static_cast<std::size_t>(start - levels.begin()));
			// A screenshot follows a level change the --time run asked for (the new level runs its
			// own --time from the start), so the menu's New Game can be checked in one shot.
			if(a.screenshot)
				change_requested_level(a);
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
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
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
		const char* draw_trace_path = std::getenv("OPENRAYMAN_DRAW_TRACE");
		if(draw_trace_path || a.recording.active())
		{
			static std::ofstream log = draw_trace_path ? std::ofstream(draw_trace_path) : std::ofstream();
			static unsigned long long frame = 0;
			std::string camera;
			if(a.live && a.camera_super_object)
				if(auto placed = openrayman::game::global_placement(*a.live, a.camera_super_object))
					camera = std::format(" camera={:.3f},{:.3f},{:.3f} back={:.4f},{:.4f},{:.4f}", placed->m[0][3], placed->m[1][3], placed->m[2][3],
						-placed->m[0][1], -placed->m[1][1], -placed->m[2][1]);
			const std::string block = std::format("DRAW {} level={} tick={} fraction={:.6f}{}\n", ++frame,
				a.game ? a.game->game_dsb().levels[a.level_index] : "", a.tick, a.script_clock * 60.0, camera)
				+ a.actor_draw_trace;
			if(log.is_open())
				log << block << std::flush;
			if(a.recording.active())
				a.recording.frames << block;
		}
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(a.window, &w, &h);
		glViewport(0, 0, w, h);
		bool level_view = a.game && a.mode == app::view::level;
		// What the level view shows: the fly camera normally, and while the scripts run the game's own
		// camera, which move_camera publishes as its super object's global transform (eye at +4, its
		// -Y axis forward, Z up). The fly camera takes that position and direction too, so Tab back
		// to flying starts from where the game camera was.
		glm::vec3 eye = a.camera.position, forward = a.camera.forward(), up(0, 0, 1);
		if(level_view && a.playing && a.live && a.camera_super_object)
			if(auto placed = openrayman::game::global_placement(*a.live, a.camera_super_object))
			{
				eye = glm::vec3(placed->m[0][3], placed->m[1][3], placed->m[2][3]);
				glm::vec3 back(-placed->m[0][1], -placed->m[1][1], -placed->m[2][1]);
				glm::vec3 top(placed->m[0][2], placed->m[1][2], placed->m[2][2]);
				if(glm::length(back) > 1e-4f)
					forward = glm::normalize(back);
				if(glm::length(top) > 1e-4f)
					up = glm::normalize(top);
				a.camera.position = eye;
				a.camera.yaw = std::atan2(forward.y, forward.x);
				a.camera.pitch = std::asin(std::clamp(forward.z, -1.0f, 1.0f));
			}
		if(level_view)
		{
			// Like the game: the background colour of the fog where the camera is; a plain sky elsewhere.
			glm::vec3 sky(0.43f, 0.59f, 0.78f);
			if(auto background = a.level->background(eye))
				sky = *background;
			glClearColor(sky.r, sky.g, sky.b, 1.0f);
		}
		else
			glClearColor(0.08f, 0.08f, 0.1f, 1.0f);
		// The clear only touches what the masks allow: a pass that left depth writes off would keep
		// last frame's depth.
		glDepthMask(GL_TRUE);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if(level_view && w > 0 && h > 0)
		{
			// The real game's renderer reported a 0.9 radian vertical angle in Learn_10 (r2live,
			// 2026-10-30). Keep the fly camera's own field of view for level exploration.
			float aspect = float(w) / float(h);
			glm::mat4 projection = a.playing && a.live && a.camera_super_object
				? glm::perspective(0.9f, aspect, 0.1f, 2000.0f) : a.camera.projection(aspect);
			glm::mat4 view = glm::lookAt(eye, eye + forward, up);
			{
				game::profiler::scope timed(game::profiler::zone::draw);
				begin_gpu_timer(a);
				light override_light;
				const light* override_fog = nullptr;
				if(a.vm && a.vm->fog_override().enabled)
				{
					const auto& fog = a.vm->fog_override();
					override_light.color = fog.colour;
					override_light.near = std::min(fog.near, fog.far);
					override_light.far = std::max(fog.near, fog.far);
					override_light.fog_near_blend = std::clamp(fog.near_blend, 0.0f, 255.0f) / 255.0f;
					override_light.fog_far_blend = std::clamp(fog.far_blend, 0.0f, 255.0f) / 255.0f;
					override_fog = &override_light;
				}
				a.level->draw(view, projection, override_fog, override_fog ? std::max(a.vm->fog_override().infinite, override_light.far) : 0.0f);
				end_gpu_timer(a);
			}
			if(a.scripts)
			{
				game::profiler::scope timed(game::profiler::zone::visibility);
				update_visibility(a, projection * view);
			}
			if(a.vm && a.dialog_overlay)
			{
				auto dialogs = game::visible_dialogs(*a.vm);
				// Screen slots are the separate menu/HUD text path (0x465C10). Their
				// text IDs are resolved by the script handlers before reaching here.
				if(a.screen_font_actor)
					for(const auto& slot : a.vm->screen_text().all())
						if(slot.used && slot.alpha && !slot.text.empty())
						{
							game::dialog_text text;
							text.prepared = slot.text;
							text.revealed = int(slot.text.size());
							text.template_actor = a.screen_font_actor;
							// 0x465DD0 computes one shared advance from the slot size.
							auto metrics = game::screen_slot_metrics(slot.size);
							text.widths.fill(float(metrics.advance));
							text.widths[0] = float(metrics.space);
							text.default_scale = 1;
							text.line_height = float(metrics.line_height);
							text.screen = true;
							text.x = slot.x; text.y = slot.y; text.z = slot.size;
							// 0x4660B0: the colour mode picks the tint (0/1 blue, 2 red, 3 yellow, as
							// the /O codes), the slot's alpha fades it (the menu greys entries out at 90),
							// and the pulse changes both vertical edges by sin(5 t) * 7 slot units.
							std::array<float, 4> tint{110 / 255.0f, 190 / 255.0f, 1, slot.alpha / 255.0f};
							if(slot.flags & game::screen_text::colour_set)
							{
								if(slot.colour == 2) tint = {250 / 255.0f, 70 / 255.0f, 70 / 255.0f, tint[3]};
								else if(slot.colour == 3) tint = {1, 1, 120 / 255.0f, tint[3]};
							}
							text.tint = tint;
							text.edge = a.vm->particles().draws_gauge(slot.owner);
							if(slot.wobble)
								text.pulse = std::sin(float(a.vm->clock()) / 1000.0f * 5.0f) * 7.0f;
							dialogs.push_back(std::move(text));
						}
				const auto& boxes = a.vm->particles().screen_rectangles();
				// The gauges (kind 120) are painted before the images (0x460670, then 0x460600).
				std::vector<game::particles::screen_image> images = a.vm->particles().screen_bar_images();
				// The gauge's actor is the in-game HUD's: its pictures and counters are the edge HUD.
				for(auto image : a.vm->particles().screen_images())
				{
					image.edge = a.vm->particles().draws_gauge(image.owner);
					images.push_back(image);
				}
				// Then the tiled picture of kinds 170/171 (0x460670 -> 0x4658B0).
				for(const auto& tile : a.vm->particles().screen_tile_images())
					images.push_back(tile);
				// OPENRAYMAN_OVERLAY_LOG: with a screenshot, what is on the screen and each piece's class
				// (edge HUD or the centred block), for tools/text_pictures.py.
				if(a.screenshot && std::getenv("OPENRAYMAN_OVERLAY_LOG"))
				{
					for(const auto& d : dialogs)
						SDL_Log("OVERLAY %s edge=%d at %.1f,%.1f,%.1f '%s'", d.tint ? "slot" : d.screen ? "text" : "world text", d.edge, d.x, d.y, d.z,
							(d.prepared.empty() ? d.text : d.prepared).c_str());
					for(const auto& b : boxes)
						SDL_Log("OVERLAY box %.1f,%.1f - %.1f,%.1f alpha %.0f", b.first[0], b.first[1], b.second[0], b.second[1], b.alpha);
					for(const auto& i : images)
						SDL_Log("OVERLAY picture %d edge=%d owner=%08X %.1f,%.1f - %.1f,%.1f alpha %.0f", i.slot, i.edge, i.owner, i.first[0], i.first[1], i.second[0], i.second[1], i.second[2]);
				}
				if(!dialogs.empty() || !boxes.empty() || !images.empty() ||
					(a.show_cutscene_bars && (a.vm->world_view_bounds().top > 0 || a.vm->world_view_bounds().bottom < 1000)))
					a.dialog_overlay->draw(dialogs, boxes, images, projection * view, projection, w, h, a.animation_time, a.vm->world_view_bounds(), a.show_cutscene_bars);
			}
		}
		else if(a.texture && w > 0 && h > 0)
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

		auto screenshot = a.screenshot;
		// Optional sequence alongside a real interactive replay, once per simulation tick.
		if(const char* directory = std::getenv("OPENRAYMAN_DRAW_CAPTURES"))
			if(const char* range = std::getenv("OPENRAYMAN_CAPTURE_TICKS"))
			{
				long from = 0, to = 0;
				static long captured = -1;
				if(std::sscanf(range, "%ld-%ld", &from, &to) == 2 && a.tick >= from && a.tick <= to && a.tick != captured)
				{
					screenshot = fs::path(directory) / std::format("tick{}.bmp", a.tick);
					captured = a.tick;
				}
			}
		if(screenshot)
		{
			SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);
			SDL_FlipSurface(surface, SDL_FLIP_VERTICAL); // GL reads bottom-up
			// The window shows colour only; the framebuffer's alpha is whatever the passes wrote
			// (solid surfaces leave it at their own alpha), so make the saved picture opaque.
			auto* bytes = static_cast<std::uint8_t*>(surface->pixels);
			for(int i = 3; i < w * h * 4; i += 4)
				bytes[i] = 255;
			if(!SDL_SaveBMP(surface, screenshot->string().c_str()))
				SDL_Log("Could not save screenshot: %s", SDL_GetError());
			SDL_DestroySurface(surface);
		}

		if(a.recording.active() && a.recording.pictures && a.tick % 4 == 0 && a.tick != a.recording.last_picture && w >= 4 && h >= 4)
		{
			// A quarter-size picture every 4 ticks, named by tick, to line up with frames.txt.
			a.recording.last_picture = a.tick;
			SDL_Surface* full = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
			SDL_Surface* small = SDL_CreateSurface(w / 4, h / 4, SDL_PIXELFORMAT_ABGR8888);
			if(full && small)
			{
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, full->pixels);
				SDL_FlipSurface(full, SDL_FLIP_VERTICAL);
				SDL_BlitSurfaceScaled(full, nullptr, small, nullptr, SDL_SCALEMODE_LINEAR);
				auto* bytes = static_cast<std::uint8_t*>(small->pixels);
				for(int i = 3; i < small->h * small->pitch; i += 4)
					bytes[i] = 255;
				SDL_SaveBMP(small, (a.recording.folder / std::format("tick{}.bmp", a.tick)).string().c_str());
			}
			SDL_DestroySurface(full);
			SDL_DestroySurface(small);
		}
		if(a.recording.active())
		{
			// A small red square in the top right corner while recording (not in the pictures).
			glEnable(GL_SCISSOR_TEST);
			glScissor(w - 28, h - 28, 16, 16);
			glClearColor(0.9f, 0.1f, 0.1f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glDisable(GL_SCISSOR_TEST);
		}
		draw_profile(a, w, h);
		game::profiler::scope timed(game::profiler::zone::swap);
		SDL_GL_SwapWindow(a.window);
	}

	// F10: start or stop a debug recording (see app::debug_recording; docs/TESTING.md).
	void toggle_recording(app& a, bool pictures)
	{
		auto& r = a.recording;
		if(r.active())
		{
			r.frames.close();
			r.sounds.close();
			r.rayman.reset();
			SDL_Log("Recording saved to %s", r.folder.string().c_str());
			update_title(a);
			return;
		}
		const char* program = SDL_GetBasePath();
		const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
		r.folder = fs::path(program ? program : ".") / "recordings" / std::format("rec_{:%Y%m%d_%H%M%S}", now);
		std::error_code error;
		fs::create_directories(r.folder, error);
		r.frames.open(r.folder / "frames.txt");
		if(!r.frames.is_open())
		{
			SDL_Log("Could not start a recording in %s", r.folder.string().c_str());
			return;
		}
		r.sounds.open(r.folder / "sounds.txt");
		const std::string level = a.game ? a.game->game_dsb().levels[a.level_index] : "";
		r.rayman = std::make_unique<openrayman::game_trace>(false);
		r.rayman->open((r.folder / "rayman.txt").string(), a.trace_vars);
		r.rayman->restart(level);
		r.pictures = pictures;
		r.last_picture = -1;
		std::ofstream(r.folder / "info.txt") << std::format(
			"OpenRayman {}\nlevel {}\nstarted at tick {}\nplaying {}\nscripts {}\npictures {}\ncamera {:.3f},{:.3f},{:.3f}\n"
			"frames.txt: every drawn frame (DRAW line: tick, camera; then every actor)\n"
			"rayman.txt: Rayman and the held keys every tick (r2live format)\nsounds.txt: actor sounds as they start\n",
			version, level, a.tick, a.playing, a.scripts, pictures, a.camera.position.x, a.camera.position.y, a.camera.position.z);
		SDL_Log("Recording to %s (F10 to stop)", r.folder.string().c_str());
		update_title(a);
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
	// Open the Steam Controller through SDL's HIDAPI driver, so it works without Steam running
	// (docs/TESTING.md has the note about Steam Input).
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");
	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenRayman", SDL_GetError(), nullptr);
		return SDL_APP_FAILURE;
	}

	auto a = std::make_unique<app>();
	int window_width = 1280, window_height = 720;
	for(int n = 1; n + 1 < argc; ++n)
		if(std::string_view(argv[n]) == "--window-size")
		{
			int w = 0, h = 0;
			if(std::sscanf(argv[n + 1], "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
			{ window_width = w; window_height = h; }
		}
	a->window = SDL_CreateWindow("OpenRayman", window_width, window_height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if(!a->window || !create_context(*a))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenRayman",
			("Could not create an OpenGL 3.3 or OpenGL ES 3.0 window. Make sure your graphics drivers are up to date.\n\n"
				+ std::string(SDL_GetError())).c_str(), nullptr);
		return SDL_APP_FAILURE;
	}
	SDL_Log("OpenGL: %s / %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

	try
	{
		a->program = gl::create_program(a->api, vertex_shader, fragment_shader);
		a->text_program = gl::create_program(a->api, vertex_shader, text_fragment_shader);
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
	try
	{
		a->level = std::make_unique<level_renderer>(a->api);
	}
	catch(const std::exception& e)
	{
		show_error(*a, e.what());
		return SDL_APP_FAILURE;
	}

	a->music = std::make_unique<music_player>();

	// The gamepads already plugged in are opened as if they had just been added, so the first one
	// gets game pad 0 (which is the one the game's actions test).
	{
		int count = 0;
		if(SDL_JoystickID* ids = SDL_GetGamepads(&count))
		{
			for(int i = 0; i < count; i++)
				open_pad(*a, ids[i]);
			SDL_free(ids);
		}
	}

	// Game folder: --game <folder>, else the last one used, else ask.
	std::optional<fs::path> folder;
	std::optional<long> start_texture;
	for(int n = 1; n < argc; ++n)
		if(std::string_view(argv[n]) == "--save" && n + 1 == argc)
		{
			SDL_Log("--save requires a slot number from 1 to 3");
			return SDL_APP_FAILURE;
		}
	for(int n = 1; n + 1 < argc; n++)
	{
		std::string arg = argv[n];
		if(arg == "--game")
			folder = fs::path(reinterpret_cast<const char8_t*>(argv[n + 1]));
		else if(arg == "--camera-log")
		{
			a->camera_log.open(fs::path(reinterpret_cast<const char8_t*>(argv[n + 1])));
			if(!a->camera_log)
				SDL_Log("Could not open camera log: %s", argv[n + 1]);
			else
				a->camera_log << "tick,player_x,player_y,player_z,camera_x,camera_y,camera_z,distance,speed\n";
		}
		else if(arg == "--vsync") // on (default: adaptive where offered), strict or off; F9 cycles
		{
			std::string mode = argv[n + 1];
			a->vsync = mode == "off" ? app::vsync_mode::off : mode == "strict" ? app::vsync_mode::strict : app::vsync_mode::on;
		}
		else if(arg == "--fps-limit") // frames per second, 0 for none
			a->fps_limit = std::max(0, int(std::strtol(argv[n + 1], nullptr, 10)));
		else if(arg == "--screenshot")
			a->screenshot = fs::path(reinterpret_cast<const char8_t*>(argv[n + 1]));
		else if(arg == "--texture")
		{
			start_texture = std::strtol(argv[n + 1], nullptr, 10);
			a->mode = app::view::textures;
		}
		else if(arg == "--level")
			a->start_level = argv[n + 1];
		else if(arg == "--actor-state") // <node>:<state>, as F4/F5 show them; may be repeated
		{
			char* end = nullptr;
			unsigned long node = std::strtoul(argv[n + 1], &end, 10);
			if(end && *end == ':')
				a->start_states.push_back({ node, std::strtoul(end + 1, nullptr, 10) });
		}
		else if(arg == "--behaviour") // <node>:<index>, as ortool brain numbers them; may be repeated
		{
			char* end = nullptr;
			unsigned long node = std::strtoul(argv[n + 1], &end, 10);
			if(end && *end == ':')
				a->start_behaviours.push_back({ node, std::strtoul(end + 1, nullptr, 10) });
		}
		else if(arg == "--keys") // name:from-to ticks, comma separated, as ortool input names keys
		{
			std::string list = argv[n + 1];
			for(std::size_t at = 0; at < list.size();)
			{
				std::size_t end = std::min(list.find(',', at), list.size());
				std::string item = list.substr(at, end - at);
				std::size_t colon = item.find(':'), dash = item.find('-');
				int k = 1;
				while(colon != std::string::npos && k < 256 && openrayman::key_name(std::uint16_t(k)) != item.substr(0, colon))
					k++;
				if(colon != std::string::npos && dash != std::string::npos && k < 256)
					a->held_keys.push_back({ std::uint8_t(k), std::strtol(item.c_str() + colon + 1, nullptr, 10), std::strtol(item.c_str() + dash + 1, nullptr, 10) });
				at = end + 1;
			}
		}
		else if(arg == "--collision") // on or off (default): what the level collides with; C in the viewer
			a->show_collision = std::string_view(argv[n + 1]) == "on";
		else if(arg == "--save")
		{
			std::string_view slot = argv[n + 1];
			if(slot != "1" && slot != "2" && slot != "3")
			{
				SDL_Log("--save requires a slot number from 1 to 3");
				return SDL_APP_FAILURE;
			}
			a->save_slot = unsigned(slot[0] - '0');
		}
		else if(arg == "--start") // portal (default): as reached from the hub; new: as on a new game; intro: a new game that plays the level's opening scene (Learn_10, work in progress)
		{
			a->intro_start = std::string_view(argv[n + 1]) == "intro";
			a->new_game = a->intro_start || std::string_view(argv[n + 1]) == "new";
		}
		else if(arg == "--entry") // procedure 343's level/reason bytes for a diagnostic arrival
		{
			unsigned level = 0, reason = 0;
			if(std::sscanf(argv[n + 1], "%u,%u", &level, &reason) != 2 || level > 255 || reason > 255)
			{
				SDL_Log("--entry must be two bytes: level,reason");
				return SDL_APP_FAILURE;
			}
			a->carried_entry = {std::uint8_t(level), std::uint8_t(reason), 0, 0};
		}
		else if(arg == "--story-bits") // comma-separated story bits to set at load, e.g. 1095,1143
		{
			std::string_view list = argv[n + 1];
			for(std::size_t at = 0; at < list.size();)
			{
				std::size_t end = std::min(list.find(',', at), list.size());
				a->story_bits.push_back(std::atoi(std::string(list.substr(at, end - at)).c_str()));
				at = end + 1;
			}
		}
		else if(arg == "--time") // seconds into the actors' animations, for screenshots
			a->start_time = std::strtod(argv[n + 1], nullptr);
		else if(arg == "--trace") // a file: Rayman every tick in r2live's format (docs/TESTING.md)
			a->trace.open(argv[n + 1], a->trace_vars);
		else if(arg == "--trace-vars") // script variables for --trace, e.g. 21,27,35 (before --trace)
			a->trace_vars = argv[n + 1];
		else if(arg == "--place")
		{
			std::array<float, 4> p{};
			float at = 0;
			int read = std::sscanf(argv[n + 1], "%f,%f,%f,%f@%f", &p[0], &p[1], &p[2], &p[3], &at);
			if(read >= 4)
			{
				a->start_place = p;
				a->start_place_tick = read == 5 ? long(at * 60.0f + 0.5f) : 0;
			}
		}
		else if(arg == "--lighting") // on (default) or off: the game's lights and fog, or a plain light; L in the viewer
			a->level->set_lighting(std::string(argv[n + 1]) != "off");
		else if(arg == "--luminosity") // the game's luminosity setting (0.5 neutral); F6/F7 in the viewer
			a->level->set_luminosity(std::strtof(argv[n + 1], nullptr));
		else if(arg == "--edge-clamp") // on (default) or off, to compare texture edges; F2 in the viewer
			a->level->set_edge_clamp(std::string(argv[n + 1]) != "off");
		else if(arg == "--camera")
		{
			// x,y,z,yaw,pitch with angles in degrees, for reproducible screenshots.
			float v[5] = {};
			int parsed = 0;
			for(char* p = argv[n + 1]; parsed < 5 && *p; parsed++)
			{
				char* end = nullptr;
				v[parsed] = std::strtof(p, &end);
				if(end == p)
					break;
				p = *end == ',' ? end + 1 : end;
			}
			if(parsed == 5)
			{
				fly_camera camera;
				camera.position = glm::vec3(v[0], v[1], v[2]);
				camera.yaw = glm::radians(v[3]);
				camera.pitch = glm::radians(v[4]);
				a->start_camera = camera;
			}
		}
	}
	apply_vsync(*a);
	if(const char* log = std::getenv("OPENRAYMAN_PROFILE"))
	{
		a->profile_log.open(fs::path(reinterpret_cast<const char8_t*>(log)));
		if(!a->profile_log)
			SDL_Log("Could not open the profile log: %s", log);
		else
		{
			using game::profiler::zone;
			using game::profiler::counter;
			a->profile_log << "seconds,frames,vsync";
			for(std::size_t i = 0; i < std::size_t(zone::count); i++)
			{
				std::string z(game::profiler::name(zone(i)));
				a->profile_log << "," << z << " ms," << z << " worst ms";
			}
			for(std::size_t i = 0; i < std::size_t(counter::count); i++)
				a->profile_log << "," << game::profiler::name(counter(i)) << " per frame";
			a->profile_log << "\n";
			game::profiler::set_enabled(true);
		}
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

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			if(a.input)
				a.input->release_all();
			break;

		case SDL_EVENT_GAMEPAD_ADDED:
			open_pad(a, event->gdevice.which);
			break;

		case SDL_EVENT_GAMEPAD_REMOVED:
			close_pad(a, event->gdevice.which);
			break;

		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			// Only while playing, like the keyboard (feed_pad checks).
			for(const app::pad_slot& p : a.pads)
				if(p.id == (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION ? event->gaxis.which : event->gbutton.which))
					feed_pad(a, p);
			break;

		case SDL_EVENT_KEY_UP:
			if(a.input && a.playing)
				if(std::uint8_t key = game_key(event->key.scancode))
					a.input->set_key(key, false);
			break;

		case SDL_EVENT_KEY_DOWN:
			if(event->key.key == SDLK_F8 && !event->key.repeat)
			{
				a.show_profile = !a.show_profile;
				game::profiler::set_enabled(a.show_profile || a.profile_log.is_open());
				if(a.show_profile)
					update_profile_texture(a, { "profiling..." });
				return SDL_APP_CONTINUE;
			}
			if(event->key.key == SDLK_F10 && !event->key.repeat)
			{
				toggle_recording(a, (event->key.mod & SDL_KMOD_SHIFT) != 0);
				return SDL_APP_CONTINUE;
			}
			if(event->key.key == SDLK_F9 && !event->key.repeat)
			{
				a.vsync = a.vsync == app::vsync_mode::on ? app::vsync_mode::strict : a.vsync == app::vsync_mode::strict ? app::vsync_mode::off : app::vsync_mode::on;
				apply_vsync(a);
				return SDL_APP_CONTINUE;
			}
			if(a.mode == app::view::level && event->key.key == SDLK_TAB && !event->key.repeat)
			{
				a.playing = !a.playing && a.input;
				if(a.input)
					a.input->release_all();
				a.actor_note = a.playing ? "playing: keys go to the game, Tab to fly" : "flying";
				SDL_Log("%s", a.playing ? "Playing: the keyboard goes to the game (Tab to fly the camera)" : "Flying the camera");
				update_title(a);
				return SDL_APP_CONTINUE;
			}
			if(a.mode == app::view::level && a.playing && a.input)
			{
				// Everything but Tab goes to the game.
				if(std::uint8_t key = game_key(event->key.scancode))
					a.input->set_key(key, true);
				return SDL_APP_CONTINUE;
			}
			if(a.mode == app::view::level)
			{
				switch(event->key.key)
				{
					case SDLK_PAGEDOWN: step_level(a, 1); return SDL_APP_CONTINUE;
					case SDLK_PAGEUP: step_level(a, -1); return SDL_APP_CONTINUE;
					case SDLK_HOME: step_level(a, -static_cast<long>(a.level_index)); return SDL_APP_CONTINUE;
					case SDLK_R: load_level(a, a.level_index); return SDL_APP_CONTINUE; // back to the start position
					case SDLK_F2:
						// Debug: compare texture edges with and without clamping.
						if(a.level)
						{
							a.level->set_edge_clamp(!a.level->edge_clamp());
							SDL_Log("Texture edge clamp %s", a.level->edge_clamp() ? "on" : "off");
						}
						return SDL_APP_CONTINUE;
					case SDLK_L:
						if(a.level)
						{
							a.level->set_lighting(!a.level->lighting());
							SDL_Log("Game lighting %s", a.level->lighting() ? "on" : "off");
						}
						return SDL_APP_CONTINUE;
					case SDLK_F6:
					case SDLK_F7:
						if(a.level)
						{
							float step = event->key.key == SDLK_F7 ? 0.05f : -0.05f;
							a.level->set_luminosity(std::clamp(a.level->luminosity() + step, 0.0f, 2.0f));
							a.actor_note = std::format("luminosity {:.2f}", a.level->luminosity());
							SDL_Log("Luminosity %.2f", a.level->luminosity());
							update_title(a);
						}
						return SDL_APP_CONTINUE;
					case SDLK_F4:
					case SDLK_F5:
						step_actor_state(a, event->key.key == SDLK_F5 ? 1 : -1, (event->key.mod & SDL_KMOD_SHIFT) != 0);
						return SDL_APP_CONTINUE;
					case SDLK_C:
						a.show_collision = !a.show_collision;
						if(a.level)
							a.level->set_show_collision(a.show_collision);
						SDL_Log("Collision %s", a.show_collision ? "shown" : "hidden");
						return SDL_APP_CONTINUE;
					case SDLK_G:
						a.scripts = !a.scripts;
						a.actor_note = a.scripts ? "scripts on" : "scripts off";
						SDL_Log("Scripts %s", a.scripts ? "on" : "off");
						update_title(a);
						return SDL_APP_CONTINUE;
					case SDLK_F3:
						a.animating = !a.animating;
						SDL_Log("Animations %s at %.2f s", a.animating ? "playing" : "paused", a.animation_time);
						return SDL_APP_CONTINUE;
					case SDLK_P:
					{
						// Copy where we are, as --camera arguments, for bug reports and screenshots.
						const fly_camera& c = a.camera;
						std::string where = std::format("--level {} --camera {:.1f},{:.1f},{:.1f},{:.1f},{:.1f}", a.game->game_dsb().levels[a.level_index],
							c.position.x, c.position.y, c.position.z, glm::degrees(c.yaw), glm::degrees(c.pitch));
						SDL_SetClipboardText(where.c_str());
						SDL_Log("Camera: %s (copied to the clipboard)", where.c_str());
						return SDL_APP_CONTINUE;
					}
				}
			}
			switch(event->key.key)
			{
				case SDLK_ESCAPE: return SDL_APP_SUCCESS;
				case SDLK_T:
					a.mode = a.mode == app::view::level ? app::view::textures : app::view::level;
					update_title(a);
					break;
				case SDLK_RIGHT: step_texture(a, 1); break;
				case SDLK_LEFT: step_texture(a, -1); break;
				case SDLK_PAGEDOWN: case SDLK_DOWN: step_texture(a, 25); break;
				case SDLK_PAGEUP: case SDLK_UP: step_texture(a, -25); break;
				case SDLK_HOME: show_texture(a, 0); break;
				case SDLK_END: if(!a.textures.empty()) show_texture(a, a.textures.size() - 1); break;
				case SDLK_M: step_music(a, 1); break;
				case SDLK_N: step_music(a, -1); break;
				case SDLK_SPACE: if(a.mode == app::view::textures && a.music && a.music_index) { a.music->set_paused(!a.music->paused()); update_title(a); } break;
				case SDLK_O: pick_folder(a); break;
				case SDLK_F11:
					SDL_SetWindowFullscreen(a.window, !(SDL_GetWindowFlags(a.window) & SDL_WINDOW_FULLSCREEN));
					break;
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			if(a.mode == app::view::level && event->button.button == SDL_BUTTON_RIGHT)
			{
				a.looking = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
				SDL_SetWindowRelativeMouseMode(a.window, a.looking);
			}
			break;

		case SDL_EVENT_MOUSE_MOTION:
			if(a.mode == app::view::level && a.looking)
				a.camera.look(event->motion.xrel, event->motion.yrel);
			break;

		case SDL_EVENT_MOUSE_WHEEL:
			if(a.mode == app::view::level)
				a.speed = std::clamp(a.speed * (event->wheel.y > 0 ? 1.25f : 0.8f), 1.0f, 200.0f);
			else
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

	// Frame times in nanoseconds: at 280 fps a frame is 3.6 ms, which whole milliseconds would
	// round to 3 or 4 and make the camera and animations step unevenly.
	const Uint64 now_ns = SDL_GetTicksNS();
	if(a.frame_started_ns)
		game::profiler::add(game::profiler::zone::frame, std::chrono::nanoseconds(now_ns - a.frame_started_ns));
	a.frame_started_ns = now_ns;
	game::profiler::end_frame();
	take_profile(a);
	Uint64 now = now_ns / 1000000;
	float dt = a.last_frame ? std::min(float(now_ns - a.last_frame) / 1e9f, 0.1f) : 0.0f;
	// OPENRAYMAN_FRAMES=1 logs the average frame time every two seconds (performance checks).
	static const bool log_frames = std::getenv("OPENRAYMAN_FRAMES") != nullptr;
	static Uint64 frames_since = now;
	static int frames = 0;
	if(log_frames && ++frames && now - frames_since >= 2000)
	{
		SDL_Log("frames: %.1f ms average, %.1f fps", double(now - frames_since) / frames, frames * 1000.0 / double(now - frames_since));
		frames_since = now;
		frames = 0;
	}
	a.last_frame = now_ns;
	if(a.mode == app::view::level)
	{
		// WASD to move, Space/E up, Ctrl/Q down, Shift for faster.
		const bool* keys = SDL_GetKeyboardState(nullptr);
		if(a.playing)
		{
			static const bool none[SDL_SCANCODE_COUNT] = {};
			keys = none;
		}
		float forward = float(keys[SDL_SCANCODE_W]) - float(keys[SDL_SCANCODE_S]);
		float right = float(keys[SDL_SCANCODE_D]) - float(keys[SDL_SCANCODE_A]);
		float up = float(keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_E]) - float(keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_Q]);
		float step = a.speed * dt * (keys[SDL_SCANCODE_LSHIFT] ? 4.0f : 1.0f);
		a.camera.move(forward * step, right * step, up * step);

		if(a.animating && !a.screenshot)
		{
			a.animation_time += dt;
			game::profiler::scope timed(game::profiler::zone::scripts);
			run_scripts(a, dt);
		}
		if(a.vm && a.vm->quit_requested) return SDL_APP_SUCCESS;
		change_requested_level(a);
		game::profiler::scope timed(game::profiler::zone::posing);
		update_actors(a);
	}

	// Keep the music clock in the title ticking.
	if(a.music_index && now - a.last_title_update > 500)
	{
		a.last_title_update = now;
		update_title(a);
	}

	draw(a);
	// The frame-rate cap: wait out the rest of this frame's share. Falling more than a frame
	// behind starts the count again rather than hurrying to catch up.
	if(a.fps_limit > 0 && !a.screenshot)
	{
		const Uint64 period = 1000000000ull / Uint64(a.fps_limit), after = SDL_GetTicksNS();
		a.next_frame_ns = a.next_frame_ns && after < a.next_frame_ns + period ? a.next_frame_ns + period : after;
		if(after < a.next_frame_ns)
		{
			game::profiler::scope timed(game::profiler::zone::limiter);
			SDL_DelayPrecise(a.next_frame_ns - after);
		}
	}
	return a.screenshot ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult)
{
	std::unique_ptr<app> a(static_cast<app*>(appstate));
	if(!a)
		return;
	close_pads(*a);
	a->music.reset();
	if(a->context)
		SDL_GL_DestroyContext(a->context);
	if(a->window)
		SDL_DestroyWindow(a->window);
}
