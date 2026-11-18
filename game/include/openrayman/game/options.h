#pragma once
#include <openrayman/game/screen_text.h>
#include <filesystem>
#include <span>
#include <string>

namespace openrayman::game
{
	struct engine_options
	{
		int music = 10, effects = 10, vsync = 0, fps = 0;
		int fullscreen = 0; // 0 windowed, 1 borderless, 2 exclusive (old 1 stays borderless)
		int display_width = 0, display_height = 0, refresh_millihz = 0;
		bool loading_screens = true;
		int language = -1; // -1: use the existing language preference/prompt
		bool operator==(const engine_options&) const = default;
	};
	engine_options read_options(const std::filesystem::path& root);
	bool write_options(const std::filesystem::path& root, const engine_options& options);

	enum class option_id { sound, display, game, back, music, effects, fullscreen, display_mode, vsync, fps, loading, language, autosave };
	struct option_entry { option_id id; const char* label; };
	// Future entries belong here: cutscene bars #5, widescreen/HDR #43,
	// antialiasing #51, controller #101 and rumble #102. No placeholder controls.
	class options_page
	{
	public:
		enum class page { root, sound, display, game };
		enum class action { up, down, left, right, confirm, back };
		page current = page::root;
		int selected = 0;
		bool active = false;
		std::string error;
		struct display_mode { int width, height, refresh_millihz; bool operator==(const display_mode&) const = default; };
		std::vector<display_mode> modes;
		bool desktop_display = true;
		std::span<const option_entry> entries(const engine_options& values) const;
		// Returns true when a setting changed. Back at root closes the page.
		bool navigate(action input, engine_options& values, bool& autosave, bool has_slot);
		void draw(screen_text& slots, const engine_options& values, bool autosave, unsigned slot) const;
	};
}
