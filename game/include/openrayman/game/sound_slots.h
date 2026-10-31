#pragma once

#include <openrayman/game/live_memory.h>
#include <openrayman/game_data.h>
#include <openrayman/snd_file.h>

#include <string>

namespace openrayman::game
{
	// Writes each sound event of Fix.snd and `<level>.snd` into the slot that names it (0x44D2F0),
	// so script sound nodes and animation sound events hold event ids. The game writes event
	// pointers; we keep the id, which the sound player resolves. The stored slot addresses already
	// point into the blocks they live in (docs/formats/sound.md). Returns how many slots were filled.
	inline std::size_t apply_sound_slots(live_memory& m, const game_data& game, const std::string& level)
	{
		std::size_t filled = 0;
		for(const std::string& file : { std::string("World/Levels/Fix.snd"), "World/Levels/" + level + "/" + level + ".snd" })
		{
			auto path = game.resolve(file);
			if(!path)
				continue;
			for(const sound_slot& s : read_snd(*path))
			{
				if(!m.contains(s.slot, 4))
					continue;
				m.write<std::uint32_t>(s.slot, s.event);
				filled++;
			}
		}
		return filled;
	}
}
