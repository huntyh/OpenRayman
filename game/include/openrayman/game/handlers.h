#pragma once

#include <openrayman/game/interpreter.h>

namespace openrayman::game
{
	// Gives the interpreter every condition, function, procedure and meta-action implemented so far.
	void register_handlers(interpreter& vm);
}
