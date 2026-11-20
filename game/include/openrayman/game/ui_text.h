#pragma once
#include <array>
#include <string>
#include <string_view>

namespace openrayman::game
{
	// Columns are English, French, German, Spanish, Italian. VM language order differs.
	enum class ui_key { sound, display, game, back, music, effects, fullscreen, display_mode,
		vsync, fps, loading, language, autosave, visual_improvements, options, sound_title, display_title, game_title,
		windowed, borderless, exclusive, desktop, on, strict, off, no_save, save_error, display_error,
		yes, no, exists, load_question, delete_question, save_question, save_abandon_question, abandon_question, quit_question,
		overwrite_question, autosave_question, lum_hint, saving_hint, power_hint, continue_hint, saving,
		name_hint, english_name, french_name, spanish_name, italian_name, german_name, count };
	using ui_translation = std::array<std::string_view, 5>;
	const std::array<ui_translation, std::size_t(ui_key::count)>& ui_translations();
	std::string ui_text(ui_key key, unsigned language);
	std::string ui_language_name(unsigned language);
}
