// OpenRayman engine entry point.
//
// Phase 2: opens a window, finds the Rayman 2 data and shows a level's static geometry with a
// free-fly camera. The Phase 0 texture browser and music player are still there (T, M).

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <audio/music_player.h>
#include <camera.h>
#include <info.h>
#include <renderer/gl.h>
#include <renderer/level_renderer.h>

#include <openrayman/actor.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/game/handlers.h>
#include <openrayman/game/interpreter.h>
#include <openrayman/game/input.h>
#include <openrayman/game/live_memory.h>
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
#include <bit>
#include <cctype>
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

		// Level view, the default once a game folder is open.
		enum class view { level, textures };
		view mode = view::level;
		std::unique_ptr<level_renderer> level;
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
		bool animating = true;            // F3 pauses
		// The game's scripts, run on a live copy of the level's memory at the game's 60 ticks per
		// second. G switches them off (actors then keep their saved states).
		std::unique_ptr<openrayman::game::live_memory> live;
		std::unique_ptr<openrayman::game::interpreter> vm;
		// The keyboard, fed to the game's input actions while playing (Tab switches between that
		// and flying the camera).
		std::unique_ptr<openrayman::game::input> input;
		bool playing = false;
		// --keys Up:60-200,A:100-110: keys held over script ticks, to replay input for screenshots.
		struct held_key { std::uint8_t key; long from, to; };
		std::vector<held_key> held_keys;
		long tick = 0;
		std::vector<std::uint32_t> thinkers;     // super objects of the actors with a brain
		// The game's camera actor (the placed Fix actor with a camera part). Until the game's camera
		// runs (milestone 3c) it stands where ours is, for the scripts that work around it.
		std::uint32_t camera_super_object = 0;
		std::vector<std::uint32_t> actor_at;     // per actors.actors entry: its actor in live memory
		std::vector<std::uint32_t> super_object_at; // per entry: its super object
		// Per entry: where each of its family's states is in live memory. (The entries' own
		// locations point into the level files, which are gone once the level has loaded.)
		std::vector<std::vector<std::uint32_t>> state_addresses;
		std::vector<double> state_since;         // per entry: animation time its state began
		bool scripts = true;
		double script_clock = 0;                 // seconds not yet run as ticks
		std::string actor_note;           // the state last picked with F4/F5, shown in the title
		std::vector<std::pair<std::size_t, std::size_t>> start_states; // --actor-state <node>:<state>
		std::vector<std::pair<std::size_t, std::size_t>> start_behaviours; // --behaviour <node>:<index>
		bool show_collision = false; // C, --collision on
		float speed = 15.0f;                     // units per second
		bool looking = false;
		Uint64 last_frame = 0;
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

	// Runs the game's scripts for `seconds`, in ticks of 1/60 s, then shows the states they picked.
	// An error outside one actor's scripts (input, placing the actors) is logged and the scripts
	// stop, rather than closing the viewer.
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
			openrayman::game::step(*a.vm, a.thinkers, [](std::uint32_t self, const std::exception& e) {
				SDL_Log("Script error, actor 0x%08X: %s", self, e.what());
			});
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
					// It loops if it doesn't move on to another state when its animation ends.
					std::uint32_t after = a.live->u32(current + 0x20);
					actor.loops = after == 0 || after == current;
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
	void update_actors(app& a)
	{
		std::vector<posed_part> parts;
		for(std::size_t i = 0; i < a.actors.actors.size(); i++)
		{
			const actor_set::entry& actor = a.actors.actors[i];
			// Taken out of the world by its scripts.
			if(a.vm && a.scripts && i < a.super_object_at.size() && a.vm->removed(a.super_object_at[i]))
				continue;
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
					}
					// A state that moves on to another holds its last frame until the next tick switches.
					std::uint16_t frames = a.bank.animations[*actor.animation].frames;
					if(!actor.loops && frames)
						frame = std::min(frame, double(frames - 1));
				}
			}
			try
			{
				pose_actor(actor, a.bank, frame, parts);
			}
			catch(const std::exception&)
			{
				// Bad animation data: leave this actor out rather than stop the level.
			}
		}
		std::vector<level_renderer::part> out;
		out.reserve(parts.size());
		for(const posed_part& part : parts)
		{
			glm::mat4 model(1.0f);
			for(int r = 0; r < 3; r++)
			{
				for(int c = 0; c < 4; c++)
					model[c][r] = part.world.m[r][c];
			}
			out.push_back({ part.mesh, model });
		}
		a.level->set_parts(std::move(out));
		a.level->set_time(a.animation_time);

		std::vector<level_renderer::shadow> shadows;
		for(std::size_t i = 0; i < a.actors.actors.size(); i++)
		{
			const actor_set::entry& actor = a.actors.actors[i];
			if(a.vm && a.scripts && i < a.super_object_at.size() && a.vm->removed(a.super_object_at[i]))
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
				particles.push_back({ glm::vec3(at[0], at[1], at[2]), p.size, p.alpha / 255.0f, p.material });
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
			Uint64 start = SDL_GetTicks();
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
			// Every shadow in the game uses the same blob material, from Fix.
			const visual_material* shadow_material = nullptr;
			for(const actor_set::entry& actor : a.actors.actors)
			{
				if(actor.shadow && !shadow_material)
					shadow_material = &actor.shadow->material;
			}
			a.level_stats = a.level->load(geometry, a.actors.meshes, a.archives, std::move(lights), sectors, shadow_material);

			// The materials scripts draw particles with (procedure 503's game materials).
			std::map<std::uint32_t, visual_material> effect_materials;
			level_brains brains = read_brains(files.memory, tree);
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

			// The scripts. Rayman, the game's main actor, is the first Fix actor the level places.
			a.live = std::make_unique<openrayman::game::live_memory>(files.memory);
			std::uint32_t main_actor = header.placements.empty() ? 0 : header.placements[0].super_object.address();
			a.vm = std::make_unique<openrayman::game::interpreter>(*a.live, main_actor);
			openrayman::game::register_handlers(*a.vm);
			a.vm->set_world(openrayman::game::world_collision(std::move(collision)));
			a.input.reset();
			a.tick = 0;
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
			// --time: let the scripts run up to then, so screenshots show what they picked.
			run_scripts(a, a.animation_time);
			update_actors(a);
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

			a.game = std::move(game);
			a.archives = std::move(archives);
			a.textures = std::move(textures);
			a.music_files = a.game->music_files();
			a.music_index.reset();
			if(a.music)
				a.music->stop();
			save_folder(folder);
			show_texture(a, 0);

			const auto& levels = a.game->game_dsb().levels;
			auto start = std::ranges::find_if(levels, [&](const std::string& name) {
				return std::ranges::equal(name, a.start_level, [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
			});
			load_level(a, start == levels.end() ? 0 : static_cast<std::size_t>(start - levels.begin()));
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
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		if(level_view && w > 0 && h > 0)
			a.level->draw(glm::lookAt(eye, eye + forward, up), a.camera.projection(float(w) / float(h)));
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
		else if(arg == "--time") // seconds into the actors' animations, for screenshots
			a->start_time = std::strtod(argv[n + 1], nullptr);
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

		case SDL_EVENT_KEY_UP:
			if(a.input && a.playing)
				if(std::uint8_t key = game_key(event->key.scancode))
					a.input->set_key(key, false);
			break;

		case SDL_EVENT_KEY_DOWN:
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

	Uint64 now = SDL_GetTicks();
	float dt = a.last_frame ? std::min((now - a.last_frame) / 1000.0f, 0.1f) : 0.0f;
	a.last_frame = now;
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
			run_scripts(a, dt);
		}
		update_actors(a);
	}

	// Keep the music clock in the title ticking.
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
