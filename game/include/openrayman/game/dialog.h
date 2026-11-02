// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openrayman { struct level_files; }

namespace openrayman::game
{
	class interpreter;
	struct value;
	std::optional<std::string> resolve_dialog_value(interpreter& vm, const value& v);

	// Object-list index selected by the dialog renderer (0x409720), before bounds checking.
	std::optional<int> dialog_glyph_index(std::uint8_t byte, int offset = 0);

	struct dialog_text
	{
		std::string text;
		std::string prepared; // whole source, for measuring the unrevealed portion
		std::uint32_t template_actor = 0;
		std::array<float, 256> widths{};
		std::vector<float> scales;
		float line_height = 2, default_scale = 0;
		int revealed = 0;
		float x = 0, y = 0, z = 0;
		bool screen = false;
		std::uint32_t creator = 0;
	};

	struct dialog_glyph
	{
		int index = 0;
		std::uint8_t byte = 0;
		float x = 0, y = 0, scale = 1, advance = 1;
		bool revealed = false;
	};
	struct dialog_layout
	{
		std::vector<dialog_glyph> glyphs;
		float width = 0;
		bool centred = false;
	};
	// Layout is in the text actor's local units. The renderer supplies the screen projection.
	dialog_layout layout_dialog(const dialog_text& text);

	// The loader must provide the selected language's text IDs. Bytes are kept in game encoding.
	using dialog_resolver = std::function<std::optional<std::string>(std::int32_t)>;
	class live_memory;
	dialog_resolver make_dialog_resolver(live_memory& memory, const openrayman::level_files& files, unsigned language = 0);
	void register_dialog_handlers(interpreter& vm);
	void set_dialog_resolver(interpreter& vm, dialog_resolver resolver);
	void set_dialog_resolver(interpreter& vm, const openrayman::level_files& files, unsigned language = 0);
	std::vector<dialog_text> visible_dialogs(interpreter& vm);
}
