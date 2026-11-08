// OpenRayman engine: a per-tick log of the main actor in r2live's format. GPLv3.

#include <game_trace.h>

#include <openrayman/game/animation_player.h>

#include <cstdlib>
#include <format>
#include <sstream>
#include <utility>

namespace openrayman
{
	namespace
	{
		std::string floats(const game::live_memory& m, std::uint32_t at, int count)
		{
			std::string out;
			for(int i = 0; i < count; i++)
				out += std::format("{}{:.3f}", i ? " " : "", m.contains(at + std::uint32_t(i) * 4, 4) ? m.f32(at + std::uint32_t(i) * 4) : 0.0f);
			return out;
		}

		std::uint32_t word(const game::live_memory& m, std::uint32_t at)
		{
			return at && m.contains(at, 4) ? m.u32(at) : 0;
		}

		// A script list (array pointer, u8 count): the entry's index, or -1 (as r2live).
		int list_index(const game::live_memory& m, std::uint32_t list_at, std::uint32_t pointer, std::uint32_t size)
		{
			std::uint32_t array = word(m, list_at);
			if(!array || !pointer || pointer < array || !m.contains(list_at + 4))
				return -1;
			std::uint32_t index = (pointer - array) / size, rest = (pointer - array) % size;
			return rest == 0 && index < m.u8(list_at + 4) ? int(index) : -1;
		}

		// A game material's collide flags: +0xC collide material, its u16 at +2 (0x42DB30).
		std::uint32_t collide_flags(const game::live_memory& m, std::uint32_t material)
		{
			std::uint32_t collide = word(m, material ? material + 0xC : 0);
			return collide && m.contains(collide + 2, 2) ? m.read<std::uint16_t>(collide + 2) : 0;
		}

		// A script variable as r2live prints it: declaration +4 entries (offset, type) into the
		// variable memory's buffer (+8).
		std::string variable(const game::live_memory& m, std::uint32_t mind, std::uint32_t index)
		{
			std::uint32_t memory = word(m, mind + 0xC);
			std::uint32_t declaration = word(m, word(m, memory));
			if(!declaration || !m.contains(declaration + 0xC) || index >= m.u8(declaration + 0xC))
				return "-";
			std::uint32_t info = word(m, declaration + 4) + index * 0x10;
			std::uint32_t at = word(m, memory + 8) + word(m, info);
			std::uint32_t kind = word(m, info + 4);
			if(!m.contains(at, 4))
				return "-";
			if(kind == 0 || kind == 1) return std::to_string(int(std::int8_t(m.u8(at))));
			if(kind == 2) return std::to_string(int(m.u8(at)));
			if(kind == 5) return std::to_string(std::int32_t(m.u32(at)));
			if(kind == 7) return std::format("{:.3f}", m.f32(at));
			if(kind == 11) return floats(m, at, 3);
			return std::format("0x{:X} (type {})", m.u32(at), kind);
		}
	}

	game_trace::game_trace()
	{
		if(const char* file = std::getenv("OPENRAYMAN_TRACE"); file && *file)
		{
			const char* vars = std::getenv("OPENRAYMAN_TRACE_VARS");
			open(file, vars ? vars : "");
		}
	}

	void game_trace::open(const std::string& file, const std::string& variables)
	{
		m_out.close();
		m_out.open(file, std::ios::out | std::ios::trunc);
		m_variables.clear();
		std::stringstream list(variables.empty() ? std::string("21,27,35,87") : variables);
		for(std::string item; std::getline(list, item, ',');)
			if(!item.empty())
				m_variables.push_back(std::uint32_t(std::strtoul(item.c_str(), nullptr, 10)));
		m_last.clear();
	}

	void game_trace::restart(const std::string& level)
	{
		if(!active())
			return;
		m_out << "level " << level << "\n";
		m_last.clear();
	}

	void game_trace::tick(const game::live_memory& m, std::uint32_t super_object, std::uint32_t clock_ms, const std::string& keys)
	{
		if(!active() || !super_object)
			return;
		std::uint32_t actor = word(m, super_object + 4);
		if(!actor)
			return;
		std::vector<std::pair<std::string, std::string>> s;
		s.emplace_back("super", std::format("0x{:X}", super_object));
		if(auto p = game::global_placement(m, super_object))
		{
			s.emplace_back("at", std::format("{:.3f} {:.3f} {:.3f}", p->m[0][3], p->m[1][3], p->m[2][3]));
			auto axis = [&](int c) { return std::format("{:.3f} {:.3f} {:.3f}", p->m[0][c], p->m[1][c], p->m[2][c]); };
			s.emplace_back("axes", "X " + axis(0) + " Y " + axis(1) + " Z " + axis(2));
		}
		if(std::uint32_t data = word(m, actor))
			s.emplace_back("state", std::format("0x{:X}", word(m, data + 4)));
		std::uint32_t base = word(m, word(m, actor + 8));
		if(base && m.contains(base, 0x184))
		{
			s.emplace_back("flags", std::format("0x{:X} end 0x{:X}", m.u32(base + 8), m.u32(base + 0xC)));
			s.emplace_back("imposed", floats(m, base + 0x24, 3));
			s.emplace_back("proposed", floats(m, base + 0x30, 3));
			s.emplace_back("speed", floats(m, base + 0x3C, 3));
			s.emplace_back("gravity", floats(m, base + 0x10, 1));
			s.emplace_back("inertia", floats(m, base + 0x154, 3));
			s.emplace_back("limit", floats(m, base + 0x178, 3));
			s.emplace_back("inertia2", floats(m, base + 0x168, 4));
			// The contact report (+0x150): +4 the word the scripts read, six 0x28-byte slots from +8.
			if(std::uint32_t report = word(m, base + 0x150); report && m.contains(report, 8 + 6 * 0x28))
			{
				s.emplace_back("contact", std::format("0x{:X}", m.u32(report + 4)));
				for(std::uint32_t slot = 0; slot < 6; slot++)
				{
					std::uint32_t entry = report + 8 + slot * 0x28;
					std::uint32_t own = m.u32(entry + 0x1C), touched = m.u32(entry + 0x20);
					if(own || touched)
						s.emplace_back(std::format("slot{}", slot), std::format("n {} p {} mine 0x{:X} surface 0x{:X}",
							floats(m, entry + 4, 3), floats(m, entry + 0x10, 3), collide_flags(m, own), collide_flags(m, touched)));
				}
			}
		}
		s.emplace_back("keys", keys.empty() ? "-" : keys);
		if(std::uint32_t mind = word(m, word(m, actor + 0xC)))
		{
			std::uint32_t model = word(m, mind), normal = word(m, mind + 4), reflex = word(m, mind + 8);
			s.emplace_back("behaviour", normal ? std::to_string(list_index(m, word(m, model), word(m, normal + 8), 0xC)) : "-");
			s.emplace_back("reflex", reflex ? std::to_string(list_index(m, word(m, model + 4), word(m, reflex + 8), 0xC)) : "-");
			for(std::uint32_t v : m_variables)
				s.emplace_back(std::format("var{}", v), variable(m, mind, v));
		}
		// Changes only, as r2live: a field that comes back after missing counts as changed.
		std::string line;
		std::map<std::string, std::string> now;
		for(auto& [key, value] : s)
		{
			auto last = m_last.find(key);
			if(last == m_last.end() || last->second != value)
				line += (line.empty() ? "" : " | ") + key + " " + value;
			now[key] = value;
		}
		m_last = std::move(now);
		if(!line.empty())
			m_out << std::format("t {:>9}: ", clock_ms) << line << "\n";
	}
}
