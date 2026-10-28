// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/brain.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <format>

namespace openrayman
{
	namespace
	{
		// actor -> brain -> mind -> AI model -> behaviours -> scripts -> nodes.
		constexpr std::uint32_t actor_brain_field = 0x0C;
		namespace mind_field
		{
			constexpr std::uint32_t model = 0x00, intelligence = 0x04, reflex_intelligence = 0x08, variables = 0x0C;
		}
		namespace model_field
		{
			constexpr std::uint32_t behaviours = 0x00, reflexes = 0x04, variables = 0x08, macros = 0x0C;
		}
		// A list of behaviours or macros: the array, then a u8 count.
		constexpr std::uint32_t list_count = 0x04;
		// A macro: its script as saved, then the copy the game runs (they start out the same).
		constexpr std::uint32_t macro_size = 0x08;
		namespace behaviour_field
		{
			constexpr std::uint32_t rules = 0x00, schedule = 0x04, rule_count = 0x08; // u8
			constexpr std::uint32_t size = 0x0C;
		}
		// Intelligence: what the actor is running. +0x08 current behaviour.
		constexpr std::uint32_t intelligence_behaviour = 0x08;
		// Variable declaration: default buffer, info array, u32 buffer size, u8 count.
		namespace declaration_field
		{
			constexpr std::uint32_t defaults = 0x00, info = 0x04, size = 0x08, count = 0x0C;
		}
		constexpr std::uint32_t variable_info_size = 0x10; // u32 offset, u32 type, 2 unknown words
		// Variable memory: declaration (through the model), initial buffer, current buffer.
		constexpr std::uint32_t memory_current = 0x08;
		constexpr std::uint32_t node_size = 8; // u32 param, u16 cached skip, u8 indent, u8 type
		constexpr std::size_t max_nodes = 1 << 16;

		std::string describe(const memory_location& at)
		{
			return std::format("{} +0x{:X}", at.block->id.to_string(), at.offset);
		}

		binary_reader reader_at(const memory_location& at, std::size_t size)
		{
			if(std::size_t(at.offset) + size > at.block->data.size())
				throw format_error("data at " + describe(at) + " runs past the loaded block");
			return binary_reader(std::span(at.block->data).subspan(at.offset, size));
		}

		std::vector<std::byte> read_bytes(const memory_location& at, std::size_t size)
		{
			reader_at(at, size);
			auto begin = at.block->data.begin() + at.offset;
			return { begin, begin + std::ptrdiff_t(size) };
		}

		script read_script(const memory_image& memory, const memory_location& at)
		{
			script s;
			s.at = at;
			auto nodes = memory.follow(at);
			if(!nodes)
				throw format_error("script at " + describe(at) + " has no nodes");
			for(std::size_t i = 0;; i++)
			{
				if(i == max_nodes)
					throw format_error("script at " + describe(at) + " has no end");
				memory_location n = *nodes + std::uint32_t(i * node_size);
				binary_reader r = reader_at(n, node_size);
				script_node node;
				node.param = r.u32();
				r.skip(2);
				node.indent = r.u8();
				node.type = r.u8();
				if(node.type >= node_type_count)
					throw format_error(std::format("script node at {} has unknown type {}", describe(n), node.type));
				node.target = memory.follow(n);
				s.nodes.push_back(node);
				if(node.indent == 0)
					break;
			}
			return s;
		}

		std::vector<behaviour> read_behaviours(const memory_image& memory, const memory_location& list)
		{
			std::vector<behaviour> result;
			std::uint8_t count = reader_at(list + list_count, 1).u8();
			if(!count)
				return result;
			auto array = memory.follow(list);
			if(!array)
				throw format_error("behaviour list at " + describe(list) + " has no array");
			for(std::uint8_t i = 0; i < count; i++)
			{
				memory_location at = *array + i * behaviour_field::size;
				behaviour b;
				b.at = at;
				std::uint8_t rules = reader_at(at + behaviour_field::rule_count, 1).u8();
				if(rules)
				{
					auto first = memory.follow(at + behaviour_field::rules);
					if(!first)
						throw format_error("behaviour at " + describe(at) + " has no rules");
					for(std::uint8_t k = 0; k < rules; k++)
						b.rules.push_back(read_script(memory, *first + k * 4));
				}
				if(auto schedule = memory.follow(at + behaviour_field::schedule))
					b.schedule = read_script(memory, *schedule);
				result.push_back(std::move(b));
			}
			return result;
		}

		ai_model read_model(const memory_image& memory, const memory_location& at)
		{
			ai_model m;
			m.at = at;
			if(auto list = memory.follow(at + model_field::behaviours))
				m.behaviours = read_behaviours(memory, *list);
			if(auto list = memory.follow(at + model_field::reflexes))
				m.reflexes = read_behaviours(memory, *list);
			if(auto list = memory.follow(at + model_field::macros))
			{
				std::uint8_t count = reader_at(*list + list_count, 1).u8();
				if(count)
				{
					auto array = memory.follow(*list);
					if(!array)
						throw format_error("macro list at " + describe(*list) + " has no array");
					for(std::uint8_t i = 0; i < count; i++)
					{
						memory_location entry = *array + i * macro_size;
						// One macro in the whole game (Astro_10) is empty: both scripts are null.
						auto body = memory.follow(entry);
						m.macros.push_back({ entry, body ? read_script(memory, *body) : script{ entry, {} } });
					}
				}
			}
			if(auto declaration = memory.follow(at + model_field::variables))
			{
				binary_reader r = reader_at(*declaration + declaration_field::size, 5);
				std::uint32_t size = r.u32();
				std::uint8_t count = r.u8();
				if(auto defaults = memory.follow(*declaration + declaration_field::defaults))
					m.defaults = read_bytes(*defaults, size);
				if(count)
				{
					auto info_at = memory.follow(*declaration + declaration_field::info);
					if(!info_at)
						throw format_error("variables at " + describe(*declaration) + " have no descriptions");
					memory_location info = *info_at;
					binary_reader v = reader_at(info, count * variable_info_size);
					for(std::uint8_t i = 0; i < count; i++)
					{
						v.seek(i * variable_info_size);
						dsg_variable d;
						d.offset = v.u32();
						d.type = v.u32();
						m.variables.push_back(d);
					}
				}
			}
			return m;
		}

		std::optional<std::size_t> index_of(const std::vector<behaviour>& list, const std::optional<memory_location>& at)
		{
			if(at)
				for(std::size_t i = 0; i < list.size(); i++)
					if(list[i].at == *at)
						return i;
			return std::nullopt;
		}
	}

	level_brains read_brains(const memory_image& memory, const scene& tree)
	{
		level_brains result;
		for(std::size_t n = 0; n < tree.nodes().size(); n++)
		{
			const scene_node& node = tree.node(n);
			if(node.type != super_object_type::actor || !node.data)
				continue;
			auto brain = memory.follow(*node.data + actor_brain_field);
			if(!brain || brain->block->data.empty())
				continue;
			auto mind = memory.follow(*brain);
			if(!mind)
				continue;
			auto model_at = memory.follow(*mind + mind_field::model);
			if(!model_at)
				continue;
			std::uint64_t key = level_brains::model_key(*model_at);
			if(!result.models.contains(key))
				result.models.emplace(key, read_model(memory, *model_at));
			const ai_model& model = result.models.at(key);

			actor_brain b;
			b.node = n;
			b.model = *model_at;
			if(auto intelligence = memory.follow(*mind + mind_field::intelligence))
				b.behaviour = index_of(model.behaviours, memory.follow(*intelligence + intelligence_behaviour));
			if(auto intelligence = memory.follow(*mind + mind_field::reflex_intelligence))
				b.reflex = index_of(model.reflexes, memory.follow(*intelligence + intelligence_behaviour));
			if(auto variables = memory.follow(*mind + mind_field::variables))
				if(auto current = memory.follow(*variables + memory_current); current && !current->block->data.empty())
					if(!model.defaults.empty())
						b.variables = read_bytes(*current, model.defaults.size());
			result.actors.push_back(std::move(b));
		}
		return result;
	}

	// An actor not in the scene tree (a spawnable template): its brain and model, without a node number.
	level_brains read_brain_at(const memory_image& memory, const memory_location& actor)
	{
		level_brains result;
		auto brain = memory.follow(actor + actor_brain_field);
		if(!brain || brain->block->data.empty())
			return result;
		auto mind = memory.follow(*brain);
		if(!mind)
			return result;
		auto model_at = memory.follow(*mind + mind_field::model);
		if(!model_at)
			return result;
		std::uint64_t key = level_brains::model_key(*model_at);
		result.models.emplace(key, read_model(memory, *model_at));
		const ai_model& model = result.models.at(key);

		actor_brain b;
		b.model = *model_at;
		if(auto intelligence = memory.follow(*mind + mind_field::intelligence))
			b.behaviour = index_of(model.behaviours, memory.follow(*intelligence + intelligence_behaviour));
		if(auto intelligence = memory.follow(*mind + mind_field::reflex_intelligence))
			b.reflex = index_of(model.reflexes, memory.follow(*intelligence + intelligence_behaviour));
		if(auto variables = memory.follow(*mind + mind_field::variables))
			if(auto current = memory.follow(*variables + memory_current); current && !current->block->data.empty())
				if(!model.defaults.empty())
					b.variables = read_bytes(*current, model.defaults.size());
		result.actors.push_back(std::move(b));
		return result;
	}
}
