// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace openrayman { struct level_files; }

namespace openrayman::game
{
	class interpreter;

	struct dialog_text
	{
		std::string text;
		std::vector<int> colours; // one colour code per byte, including line breaks
		int revealed = 0;
		float x = 0, y = 0, z = 0;
		bool screen = false;
		bool centred = false;
		std::uint32_t creator = 0;
	};

	// The loader must provide the selected language's text IDs. Bytes are kept in game encoding.
	using dialog_resolver = std::function<std::optional<std::string>(std::int32_t)>;
	class live_memory;
	dialog_resolver make_dialog_resolver(live_memory& memory, const openrayman::level_files& files, unsigned language = 0);
	void register_dialog_handlers(interpreter& vm);
	void set_dialog_resolver(interpreter& vm, dialog_resolver resolver);
	void set_dialog_resolver(interpreter& vm, const openrayman::level_files& files, unsigned language = 0);
	std::vector<dialog_text> visible_dialogs(interpreter& vm);
}
