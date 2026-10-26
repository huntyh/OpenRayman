// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/brain.h>

#include <bit>
#include <format>

namespace openrayman
{
	namespace
	{
		// Keywords by number, from their handlers in Rayman2.exe (table at 0x4A5E80). The every-N-frames
		// ifs test a frame counter (0x4A7070) and skip their block on the other frames. The switches
		// skip their block while a global flag is set (0x4A66BC, 0x4B9C34; meaning not known yet).
		constexpr const char* keywords[] = {
			"if", "if not", "if (every 2 frames)", "if (every 4 frames)", "if (every 8 frames)", "if (every 16 frames)",
			"unless switch 6", "unless switch 7", "then", "else", "jump", "me", "main actor", "no actor",
			"no super object", "no waypoint", "none (27)", "none (37)", "none (38)", "no state",
		};

		// Operators by number (table at 0x4A6398): how to write them with their arguments.
		constexpr const char* operators[] = {
			"({} + {})", "({} - {})", "({} * {})", "({} / {})", "-{}",
			"{} += {}", "{} -= {}", "{} *= {}", "{} /= {}", "{}++", "{}--", "{} := {}",
			"{}.{}", "{}.x", "{}.y", "{}.z",
			"({} + {})", "({} - {})", "({} * {})", "({} / {})", "-{}",
			"{}.x := {}", "{}.y := {}", "{}.z := {}",
			"{}: {}", "({1} as {0})", "{}[{}]", "{}[{}] := {}",
		};

		std::string where(const memory_location& at)
		{
			return std::format("{} +0x{:X}", at.block->id.to_string(), at.offset);
		}

		class printer
		{
		public:
			printer(const script& s, const ai_model& model, const memory_image& memory, const scene& tree)
				: m_nodes(s.nodes), m_model(model), m_memory(memory), m_tree(tree) {}

			std::vector<std::string> run()
			{
				block(0, 1, 0);
				return std::move(m_lines);
			}

		private:
			// The statements at one indent, starting at `first`. Returns the index after them.
			std::size_t block(std::size_t first, std::uint8_t indent, int depth)
			{
				std::size_t i = first;
				while(i < m_nodes.size() && m_nodes[i].indent == indent)
				{
					const script_node& n = m_nodes[i];
					std::string pad(std::size_t(depth) * 2, ' ');
					// then, else, and the two switches guard the statements one indent deeper.
					if(n.type == std::uint8_t(node_type::keyword) && n.param >= 6 && n.param <= 9)
					{
						m_lines.push_back(pad + keywords[n.param]);
						i = block(i + 1, std::uint8_t(indent + 1), depth + 1);
						continue;
					}
					m_lines.push_back(pad + expression(i));
					i = after(i);
				}
				return i;
			}

			// The node after a node and all its arguments.
			std::size_t after(std::size_t i) const
			{
				std::size_t j = i + 1;
				while(j < m_nodes.size() && m_nodes[j].indent > m_nodes[i].indent)
					j++;
				return j;
			}

			std::vector<std::string> arguments(std::size_t i)
			{
				std::vector<std::string> result;
				for(std::size_t j = i + 1; j < m_nodes.size() && m_nodes[j].indent > m_nodes[i].indent; j = after(j))
					result.push_back(expression(j));
				return result;
			}

			static std::string call(const std::string& name, const std::vector<std::string>& args)
			{
				std::string text = name + "(";
				for(std::size_t k = 0; k < args.size(); k++)
					text += (k ? ", " : "") + args[k];
				return text + ")";
			}

			std::string pointer(const script_node& n, const char* kind) const
			{
				return n.target ? std::format("{} @{}", kind, where(*n.target)) : std::format("{} {:#x}", kind, n.param);
			}

			std::string expression(std::size_t i)
			{
				const script_node& n = m_nodes[i];
				std::vector<std::string> args = arguments(i);
				switch(node_type(n.type))
				{
				case node_type::keyword:
					if(n.param < std::size(keywords))
					{
						std::string text = keywords[n.param];
						for(const std::string& a : args)
							text += " " + a;
						return text;
					}
					break;
				case node_type::condition: return call(std::format("condition_{}", n.param), args);
				case node_type::function: return call(std::format("function_{}", n.param), args);
				case node_type::procedure: return call(std::format("procedure_{}", n.param), args);
				case node_type::meta_action: return call(std::format("meta_action_{}", n.param), args);
				case node_type::operator_:
					if(n.param < std::size(operators))
					{
						// "{}" takes the next argument, "{0}" and "{1}" a given one.
						std::string_view f = operators[n.param];
						std::size_t holes = 0;
						for(std::size_t k = 0; k < f.size(); k++)
							if(f[k] == '{')
								holes++;
						if(holes == args.size())
						{
							std::string text;
							std::size_t a = 0;
							for(std::size_t k = 0; k < f.size(); k++)
								if(f[k] == '{' && f[k + 1] == '}')
									text += args[a++], k++;
								else if(f[k] == '{')
									text += args[std::size_t(f[k + 1] - '0')], k += 2;
								else
									text += f[k];
							return text;
						}
					}
					return call(std::format("operator_{}", n.param), args);
				case node_type::field: return std::format("field_{}", n.param);
				case node_type::variable:
				case node_type::variable_10: return std::format("var{}", n.param);
				case node_type::integer: return std::to_string(std::int32_t(n.param));
				case node_type::real: return std::format("{}", std::bit_cast<float>(n.param));
				case node_type::vector: return call("vector", args);
				case node_type::mask: return std::format("mask {:#x}", n.param);
				case node_type::text:
					if(n.target)
					{
						const auto& data = n.target->block->data;
						std::string text;
						for(std::size_t k = n.target->offset; k < data.size() && data[k] != std::byte(0); k++)
							text += char(data[k]);
						return '"' + text + '"';
					}
					break;
				case node_type::actor:
					if(n.target)
						for(std::size_t k = 0; k < m_tree.nodes().size(); k++)
							if(m_tree.node(k).data == n.target)
								return std::format("actor {}", k);
					return pointer(n, "actor");
				case node_type::behaviour:
					for(std::size_t k = 0; k < m_model.behaviours.size(); k++)
						if(n.target == m_model.behaviours[k].at)
							return std::format("behaviour {}", k);
					for(std::size_t k = 0; k < m_model.reflexes.size(); k++)
						if(n.target == m_model.reflexes[k].at)
							return std::format("reflex {}", k);
					return pointer(n, "other model's behaviour");
				case node_type::subroutine:
					for(std::size_t k = 0; k < m_model.macros.size(); k++)
						if(n.target == m_model.macros[k].at)
							return call(std::format("macro {}", k), args);
					return call(pointer(n, "macro"), args);
				case node_type::family: return pointer(n, "family");
				case node_type::state: return pointer(n, "state");
				case node_type::super_object:
					if(n.target)
						if(auto k = m_tree.find(*n.target))
							return std::format("super object {}", *k);
					return pointer(n, "super object");
				case node_type::waypoint: return pointer(n, "waypoint");
				case node_type::model: return pointer(n, "model");
				case node_type::input: return pointer(n, "input");
				case node_type::end: return "end";
				default: break;
				}
				std::string text = n.target ? std::format("type{} @{}", n.type, where(*n.target)) : std::format("type{} {:#x}", n.type, n.param);
				return args.empty() ? text : call(text, args);
			}

			const std::vector<script_node>& m_nodes;
			const ai_model& m_model;
			const memory_image& m_memory;
			const scene& m_tree;
			std::vector<std::string> m_lines;
		};
	}

	std::vector<std::string> describe_script(const script& s, const ai_model& model, const memory_image& memory, const scene& tree)
	{
		return printer(s, model, memory, tree).run();
	}
}
