// OpenRayman engine: a per-tick log of the main actor in r2live's format. GPLv3.
//
// --trace <file> (or OPENRAYMAN_TRACE=<file>; variables with --trace-vars 21,27,35 or
// OPENRAYMAN_TRACE_VARS, default 21,27,35,87) writes, every 60 Hz tick
// while the scripts run, what changed of Rayman: position and axes, state, dynamics flags, speeds,
// gravity, inertia, the contact report, behaviour/reflex, chosen script variables and the held
// keys. The lines read like tools/r2live.py's (`t <clock ms>: key value | key value`), so the
// same scripts compare our game with the real one.
#pragma once

#include <openrayman/game/live_memory.h>

#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace openrayman
{
	class game_trace
	{
	public:
		// From the environment; inactive when OPENRAYMAN_TRACE isn't set.
		game_trace() : game_trace(true) {}
		// `from_environment` false: closed until open() (F10's debug recording keeps its own).
		explicit game_trace(bool from_environment);
		// Start writing to `file` (the --trace switch); `variables` as "21,27,35", empty for the default.
		void open(const std::string& file, const std::string& variables);
		bool active() const { return m_out.is_open(); }
		// A new level: the next line is written in full again.
		void restart(const std::string& level);
		// One tick: `keys` names the held keys (for the log only).
		void tick(const game::live_memory& m, std::uint32_t main_actor, std::uint32_t clock_ms, const std::string& keys);

	private:
		std::ofstream m_out;
		std::vector<std::uint32_t> m_variables;
		std::map<std::string, std::string> m_last;
	};
}
