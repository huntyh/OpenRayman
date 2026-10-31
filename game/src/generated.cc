#include <openrayman/game/generated.h>
#include <openrayman/game/animation_player.h>

#include <algorithm>
#include <cstring>

namespace openrayman::game
{
	namespace
	{
		// Sizes of what a generated actor gets of its own (the allocators 0x40BCC0's copy callbacks
		// use). Everything else (family, states, object lists, the AI model, the scripts) is shared
		// with the template.
		constexpr std::uint32_t super_object_size = 0x48;
		constexpr std::uint32_t transform_size = 0x58;
		constexpr std::uint32_t actor_size = 0x38;         // 14 component pointers
		constexpr std::uint32_t data_3d_size = 0x118;      // 0x4186A0
		constexpr std::uint32_t standard_game_size = 0x34; // 0x413130
		constexpr std::uint32_t dynamics_size = 0x0C;      // +0 base block, +4 a 0x1C block (0x412C70)
		constexpr std::uint32_t dynamics_extra_size = 0x1C;
		constexpr std::uint32_t report_size = 0x16C;       // base +0x150 with flag 0x4000 (0x412CA0)
		constexpr std::uint32_t brain_size = 0x0C;         // +0 mind, +4 0xCDCDCDCD, +8 0 (0x417E80)
		constexpr std::uint32_t mind_size = 0x14;
		constexpr std::uint32_t intelligence_size = 0x18;
		constexpr std::uint32_t variables_size = 0x10;
		constexpr std::uint32_t action_size = 0x68;
		constexpr std::uint32_t collide_size = 0x68;       // 0x4174E0; the copy leaves the last 0x24 (0x417710)
		constexpr std::uint32_t collide_copied = 0x44;
		constexpr std::uint32_t sector_info_size = 0x14;   // actor +0x20 (0x412330)
		constexpr std::uint32_t first_id = 0x10000;        // standard game +8 of a generated actor

		// The base block's size by its flags at +0xC (0x412CA0): 0x4 complex, 0x2 advanced.
		std::uint32_t base_size(std::uint32_t flags)
		{
			return flags & 0x4 ? 0x338 : flags & 0x2 ? 0x1DC : 0x154;
		}
	}

	std::uint32_t generated_actors::create(live_memory& m, std::uint32_t model, std::uint32_t creator, const std::array<float, 3>& at, std::uint32_t clock)
	{
		if(!model || !m.contains(model, actor_size))
			return 0;
		// A free slot, preferring one that held the same template (its memory fits as it is).
		std::size_t index = m_slots.size();
		for(std::size_t i = 0; i < m_slots.size(); i++)
		{
			if(m_slots[i].creator)
				continue;
			if(index == m_slots.size() || m_slots[i].model == model)
				index = i;
			if(m_slots[i].model == model)
				break;
		}
		if(index == m_slots.size())
		{
			if(m_slots.size() >= capacity)
				return 0;
			m_slots.emplace_back();
		}
		slot& s = m_slots[index];
		if(s.model != model)
			s.memory.clear();
		s.model = model;
		std::size_t next = 0;
		// The memory for the next part: the slot's own from last time, or a fresh block.
		auto take = [&](std::uint32_t size) {
			if(next == s.memory.size())
				s.memory.push_back(m.allocate(size));
			std::uint32_t to = s.memory[next++];
			std::memset(m.span(to, size).data(), 0, size);
			return to;
		};
		auto copy = [&](std::uint32_t from, std::uint32_t size) {
			std::uint32_t to = take(size);
			if(from && m.contains(from, size))
				std::memcpy(m.span(to, size).data(), m.span(from, size).data(), size);
			return to;
		};

		std::uint32_t so = take(super_object_size);
		std::uint32_t local = take(transform_size);
		std::uint32_t global = take(transform_size);
		std::uint32_t actor = copy(model, actor_size);
		m.write<std::uint32_t>(so + 0x00, 2); // an actor
		m.write<std::uint32_t>(so + 0x04, actor);
		m.write<std::uint32_t>(so + 0x20, local);
		m.write<std::uint32_t>(so + 0x24, global);
		m.write<std::uint32_t>(so + 0x2C, 0xFFFFFFFF);
		affine place;
		for(int i = 0; i < 3; i++)
			place.m[i][3] = at[std::size_t(i)];
		set_global_placement(m, so, place);
		std::memcpy(m.span(local, transform_size).data(), m.span(global, transform_size).data(), transform_size);

		// 3D data (0x4186F0): a copy, but what belongs to the instance (+0x90, +0x98, +0x9C, +0xB8,
		// u16 +0x82) starts empty; the state clock starts now (+0xBC, +0xC4). What it draws is the
		// object list at +0x10 (the pose code 0x40E0D0 reads it; procedure 94 changes it).
		if(std::uint32_t from = m.u32(model + 0x00))
		{
			std::uint32_t data = copy(from, data_3d_size);
			for(std::uint32_t off : { 0x90u, 0x98u, 0x9Cu, 0xB8u })
				m.write<std::uint32_t>(data + off, 0);
			m.write<std::uint16_t>(data + 0x82, 0);
			// Its animation hasn't played yet (the template's may have run out: +0x8E, +0x8F); mode 5
			// starts the copy's animation afresh (0x418340 -> 0x418850).
			m.write<std::uint8_t>(data + data_3d::finished, 0);
			m.write<std::uint8_t>(data + data_3d::reached_end, 0);
			// A template saved without a current state starts in its initial one (+0).
			if(!m.u32(data + data_3d::current_state))
				for(std::uint32_t off : { data_3d::current_state, data_3d::requested_state, data_3d::next_state })
					m.write<std::uint32_t>(data + off, m.u32(data));
			m.write<std::uint32_t>(data + 0xBC, clock);
			m.write<std::uint32_t>(data + 0xC4, clock);
			m.write<std::uint32_t>(actor + 0x00, data);
		}
		// Standard game (0x4136A0, then 0x40BCC0): its id, the way back to the super object, flags
		// 0x02 and 0x04 in +0x29; +0x18 cleared (0x4132A0).
		if(std::uint32_t from = m.u32(model + 0x04))
		{
			std::uint32_t standard = copy(from, standard_game_size);
			m.write<std::uint32_t>(standard + 0x08, first_id + std::uint32_t(index));
			m.write<std::uint32_t>(standard + 0x0C, so);
			m.write<std::uint32_t>(standard + 0x18, 0);
			m.write<std::uint8_t>(standard + 0x29, std::uint8_t(m.u8(standard + 0x29) | 0x06));
			m.write<std::uint32_t>(actor + 0x04, standard);
		}
		// Dynamics (0x412FF0): the wrapper, its base block (sized by its flags), the contact report
		// behind +0x150 when flag 0x4000 says there is one, and the wrapper's 0x1C block. Speeds are
		// copied, not cleared (mode 5 doesn't reset them, 0x412F20).
		if(std::uint32_t from = m.u32(model + 0x08))
		{
			std::uint32_t dynamics = copy(from, dynamics_size);
			if(std::uint32_t base_from = m.u32(from))
			{
				std::uint32_t flags = m.u32(base_from + 0x0C);
				std::uint32_t base = copy(base_from, base_size(flags));
				std::uint32_t report_from = m.u32(base_from + 0x150);
				m.write<std::uint32_t>(base + 0x150, flags & 0x4000 && report_from ? copy(report_from, report_size) : 0);
				m.write<std::uint32_t>(dynamics + 0x00, base);
			}
			if(std::uint32_t extra = m.u32(from + 0x04))
				m.write<std::uint32_t>(dynamics + 0x04, copy(extra, dynamics_extra_size));
			m.write<std::uint32_t>(actor + 0x08, dynamics);
		}
		// Brain (0x4180D0 -> 0x466ED0): a mind of its own with fresh intelligences (each in its
		// initial behaviour, nothing in progress) and fresh variables from the initial values.
		if(std::uint32_t from = m.u32(model + 0x0C))
		{
			std::uint32_t brain = copy(from, brain_size);
			m.write<std::uint32_t>(brain + 0x04, 0xCDCDCDCD);
			m.write<std::uint8_t>(brain + 0x08, 0);
			if(std::uint32_t mind_from = m.u32(from))
			{
				std::uint32_t mind = copy(mind_from, mind_size);
				for(std::uint32_t which : { 0x04u, 0x08u })
				{
					std::uint32_t intelligence_from = m.u32(mind_from + which);
					if(!intelligence_from)
						continue;
					// +4 where the schedule is, +8 the behaviour, +0xC the one before, +0x10 actions in
					// progress, +0x14 the initial behaviour.
					std::uint32_t intelligence = copy(intelligence_from, intelligence_size);
					std::uint32_t initial = m.u32(intelligence_from + 0x14);
					m.write<std::uint32_t>(intelligence + 0x04, 0);
					m.write<std::uint32_t>(intelligence + 0x08, initial);
					m.write<std::uint32_t>(intelligence + 0x0C, initial);
					if(std::uint32_t actions_from = m.u32(intelligence_from + 0x10))
					{
						// Entries pointer, u8 capacity, u8 used, u8 current: the same capacity, empty.
						std::uint8_t entries = m.u8(actions_from + 4);
						std::uint32_t actions = take(8);
						m.write<std::uint32_t>(actions, take(std::uint32_t(entries) * action_size));
						m.write<std::uint8_t>(actions + 4, entries);
						m.write<std::uint32_t>(intelligence + 0x10, actions);
					}
					m.write<std::uint32_t>(mind + which, intelligence);
				}
				// Variables: +0 the model's declaration (through a pointer; its byte size at +8), +4
				// the initial values, +8 the current ones.
				if(std::uint32_t variables_from = m.u32(mind_from + 0x0C))
				{
					std::uint32_t variables = copy(variables_from, variables_size);
					std::uint32_t declaration = m.u32(m.u32(variables_from));
					std::uint32_t size = m.u32(declaration + 8);
					std::uint32_t initial = m.u32(variables_from + 4);
					m.write<std::uint32_t>(variables + 8, copy(initial ? initial : m.u32(variables_from + 8), size));
					m.write<std::uint32_t>(mind + 0x0C, variables);
				}
				m.write<std::uint8_t>(mind + 0x10, 0);
				m.write<std::uint32_t>(brain, mind);
			}
			m.write<std::uint32_t>(actor + 0x0C, brain);
		}
		// Collision set (0x417710): only its first 0x44 bytes are copied.
		if(std::uint32_t from = m.u32(model + 0x14))
		{
			std::uint32_t collide = take(collide_size);
			std::memcpy(m.span(collide, collide_copied).data(), m.span(from, collide_copied).data(), collide_copied);
			m.write<std::uint32_t>(actor + 0x14, collide);
		}
		select_zone_groups(m, actor);
		// Which sector it is in (actor +0x20).
		if(std::uint32_t from = m.u32(model + 0x20))
			m.write<std::uint32_t>(actor + 0x20, copy(from, sector_info_size));
		// The other parts (+0x10 camera, +0x18.., none on the shot) stay shared with the template.

		s.super_object = so;
		s.creator = creator ? creator : so;
		return so;
	}

	bool generated_actors::release(std::uint32_t super_object)
	{
		for(slot& s : m_slots)
		{
			if(s.creator && s.super_object == super_object)
			{
				s.creator = 0;
				return true;
			}
		}
		return false;
	}

	std::size_t generated_actors::count(std::uint32_t creator) const
	{
		return std::size_t(std::ranges::count_if(m_slots, [&](const slot& s) { return s.creator && s.creator == creator; }));
	}

	std::vector<std::uint32_t> generated_actors::alive() const
	{
		std::vector<std::uint32_t> out;
		for(const slot& s : m_slots)
			if(s.creator)
				out.push_back(s.super_object);
		return out;
	}
}
