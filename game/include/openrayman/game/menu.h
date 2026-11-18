#pragma once
#include <cstdint>
namespace openrayman::game
{
	class interpreter;
	class input;
	// Route Escape on script sub-pages to their own Backspace action.
	void install_menu_back_routing(interpreter& vm, input& keys, std::uint32_t controller);
	inline constexpr std::uint32_t menu_move_sound = 29, menu_confirm_sound = 28;
	// Adds a virtual last row without changing the scripts' save-slot arrays.
	class menu_back_entry
	{
	public:
		static constexpr int slot = 7;
		bool active = false, selected = false;
		bool update(interpreter& vm, input& keys, std::uint32_t controller);
		bool select(interpreter& vm, std::uint32_t controller, bool back);
	private:
		std::uint32_t behaviour = 0;
		bool up_down = false, down_down = false;
		bool hint_visible = false;
	};
}
