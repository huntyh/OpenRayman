#pragma once

#include <openrayman/game/interpreter.h>

namespace openrayman::game
{
	// Gives the interpreter every condition, function, procedure and meta-action implemented so far.
	void register_handlers(interpreter& vm);

	// Rayman is a Fix actor, so his variables carry over from the level before. Loading a level
	// directly leaves them as saved; this sets the ones that change how he enters: var106 ("he
	// came through a portal", set by his scripts while he has hit points) makes him spin out of
	// the start portal (state 04/00 +0x739C) instead of dropping.
	void arrive_from_previous_level(interpreter& vm);
}
