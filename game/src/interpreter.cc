#include <openrayman/game/interpreter.h>
#include <openrayman/brain.h>
#include <openrayman/error.h>

#include <bit>
#include <cmath>
#include <format>

namespace openrayman::game
{
	namespace
	{
		namespace super_object_field
		{
			constexpr std::uint32_t type = 0x00, data = 0x04, global_transform = 0x24;
		}
		namespace actor_field
		{
			constexpr std::uint32_t data_3d = 0x00, dynamics = 0x08, brain = 0x0C;
		}
		namespace data_3d_field
		{
			constexpr std::uint32_t brain_period = 0xFE, brain_counter = 0xFF; // u8: think every period+1 frames
			constexpr std::uint32_t frame = 0x100;                            // u16: frames thought so far
			constexpr std::uint32_t shadow_size = 0xD4;                       // 2 floats
		}
		namespace mind_field
		{
			constexpr std::uint32_t model = 0x00, normal = 0x04, reflex = 0x08, variables = 0x0C;
			constexpr std::uint32_t running_normal = 0x10; // u8
		}
		namespace intelligence_field
		{
			constexpr std::uint32_t schedule_at = 0x04, behaviour = 0x08, previous = 0x0C, actions = 0x10;
		}
		namespace behaviour_field
		{
			constexpr std::uint32_t rules = 0x00, schedule = 0x04, rule_count = 0x08;
		}
		// The table of actions in progress: entries, u8 capacity, u8 used, u8 current.
		namespace actions_field
		{
			constexpr std::uint32_t entries = 0x00, capacity = 0x04, used = 0x05, current = 0x06;
		}
		// An action in progress: 0x60 bytes of scratch, then where it is and its signal state.
		namespace action_field
		{
			constexpr std::uint32_t node = 0x60, used = 0x64, script = 0x65, pending = 0x66, signal = 0x67;
			constexpr std::uint32_t size = 0x68, scratch = 0x60;
		}
		// Variable memory: model's declaration (through a pointer), initial buffer, current buffer.
		constexpr std::uint32_t variables_current = 0x08;
		constexpr std::uint32_t declaration_info = 0x04;
		constexpr std::uint32_t variable_info_size = 0x10;
		constexpr std::uint32_t node_size = 8;

		// Global registers scripts read and write as fields 11..17 (0x4B9B68..0x4B9B84).
		struct registers
		{
			std::array<float, 3> vector{};
			std::int16_t short_a = 0, short_b = 0;
			float real_a = 0, real_b = 0, real_c = 0;
			std::int32_t integer = 0;
		};
		registers g_registers;

		bool is_float_math(value& a, value& b)
		{
			// Mixed integer and float arithmetic happens in floats (0x482F10).
			if(a.is_real() || b.is_real())
			{
				a = value::real(a.as_float());
				b = value::real(b.as_float());
				return true;
			}
			return false;
		}
	}

	value value::real(float v)
	{
		return { { std::bit_cast<std::uint32_t>(v), 0, 0 }, value_type::real };
	}

	value value::vector(float x, float y, float z)
	{
		return { { std::bit_cast<std::uint32_t>(x), std::bit_cast<std::uint32_t>(y), std::bit_cast<std::uint32_t>(z) }, value_type::vector };
	}

	float value::as_float() const
	{
		return is_real() ? std::bit_cast<float>(words[0]) : float(std::int32_t(words[0]));
	}

	std::int32_t value::as_int() const
	{
		return is_real() ? std::int32_t(std::bit_cast<float>(words[0])) : std::int32_t(words[0]);
	}

	float value::component(int i) const
	{
		return std::bit_cast<float>(words[std::size_t(i)]);
	}

	interpreter::interpreter(live_memory& memory, std::uint32_t main_actor)
		: m_memory(memory), m_main_actor(main_actor)
	{
	}

	interpreter::node_view interpreter::node(std::uint32_t address) const
	{
		node_view n;
		n.param = m_memory.u32(address);
		n.cache = m_memory.read<std::uint16_t>(address + 4);
		n.indent = m_memory.u8(address + 6);
		n.type = m_memory.u8(address + 7);
		return n;
	}

	std::uint32_t interpreter::skip(std::uint32_t at) const
	{
		std::uint8_t indent = node(at).indent;
		at += node_size;
		while(node(at).indent > indent)
			at += node_size;
		return at;
	}

	std::uint32_t interpreter::actor_of(std::uint32_t super_object) const
	{
		if(!super_object || !m_memory.contains(super_object, 8) || m_memory.u32(super_object + super_object_field::type) != 2)
			return 0;
		return m_memory.u32(super_object + super_object_field::data);
	}

	std::uint32_t interpreter::super_object_of(std::uint32_t actor) const
	{
		// The game keeps a back pointer in the actor's standard game structure (0x4136D0).
		// Scripts can hold stale or not-yet-set references (while handlers are missing): read those
		// as "nobody" instead of failing.
		if(!actor || !m_memory.contains(actor + 0x04, 4))
			return 0;
		std::uint32_t standard = m_memory.u32(actor + 0x04);
		return standard && m_memory.contains(standard + 0x0C, 4) ? m_memory.u32(standard + 0x0C) : 0;
	}

	std::uint32_t interpreter::target(std::uint32_t self, bool allow_override)
	{
		// 0x4825B0: an "actor: ..." operator before this call names someone else.
		if(m_acting)
		{
			if(!allow_override)
				return self;
			std::uint32_t other = m_acting;
			m_acting = 0;
			return other;
		}
		return self;
	}

	bool interpreter::running_normal(std::uint32_t self) const
	{
		std::uint32_t mind = m_memory.u32(m_memory.u32(actor_of(self) + actor_field::brain));
		return m_memory.u8(mind + mind_field::running_normal) != 0;
	}

	void interpreter::note_missing(const char* kind, std::uint32_t index)
	{
		missing[std::format("{} {}", kind, index)]++;
	}

	// --- Evaluation ----------------------------------------------------------------------------

	std::uint32_t interpreter::eval(std::uint32_t self, std::uint32_t at, value& out)
	{
		m_last_node = at;
		node_view n = node(at);
		std::uint32_t next = at + node_size;
		switch(n.type)
		{
		case std::uint8_t(node_type::keyword):
			return keyword(self, at, out);
		case std::uint8_t(node_type::condition):
		{
			bool result = false;
			auto h = m_conditions.find(n.param);
			call c{ *this, self, next, n.param };
			if(h == m_conditions.end())
			{
				note_missing("condition", n.param);
				next = skip(at);
			}
			else
				next = h->second(c, result);
			out = value::integer(result ? 1 : 0);
			return next;
		}
		case std::uint8_t(node_type::operator_):
			return operation(self, at, out);
		case std::uint8_t(node_type::function):
		{
			auto h = m_functions.find(n.param);
			call c{ *this, self, next, n.param };
			out = value::integer(0);
			if(h == m_functions.end())
			{
				note_missing("function", n.param);
				return skip(at);
			}
			return h->second(c, out);
		}
		case std::uint8_t(node_type::procedure):
		{
			auto h = m_procedures.find(n.param);
			call c{ *this, self, next, n.param };
			m_procedure_flags = 0;
			if(h == m_procedures.end())
			{
				note_missing("procedure", n.param);
				next = skip(at);
			}
			else
				next = h->second(c);
			out = value::of(value_type::nothing, m_procedure_flags & ~flow::done);
			return next;
		}
		case std::uint8_t(node_type::meta_action):
			return meta_action(self, at, out);
		case 6: case 7:
			out = value::of(value_type::nothing, 0);
			return next;
		case std::uint8_t(node_type::end):
			out = value::of(value_type::nothing, flow::end);
			return at;
		case std::uint8_t(node_type::field):
		{
			std::uint32_t actor = actor_of(self);
			switch(n.param)
			{
			case 0:
			{
				std::uint32_t t = m_memory.u32(self + super_object_field::global_transform);
				out = value::vector(m_memory.f32(t + 4), m_memory.f32(t + 8), m_memory.f32(t + 12));
				break;
			}
			case 7: case 8:
			{
				std::uint32_t mind = m_memory.u32(m_memory.u32(actor + actor_field::brain));
				std::uint32_t intelligence = m_memory.u32(mind + (n.param == 7 ? mind_field::normal : mind_field::reflex));
				out = value::of(value_type::behaviour, intelligence ? m_memory.u32(intelligence + intelligence_field::previous) : 0);
				break;
			}
			case 9: case 10:
				out = value::real(m_memory.f32(m_memory.u32(actor) + data_3d_field::shadow_size + (n.param - 9) * 4));
				break;
			case 11: out = value::vector(g_registers.vector[0], g_registers.vector[1], g_registers.vector[2]); break;
			case 12: out = value::integer(g_registers.short_a); break;
			case 13: out = value::integer(g_registers.short_b); break;
			case 14: out = value::real(g_registers.real_a); break;
			case 15: out = value::real(g_registers.real_b); break;
			case 16: out = value::real(g_registers.real_c); break;
			case 17: out = value::integer(g_registers.integer); break;
			default:
				note_missing("field", n.param);
				out = value::integer(0);
				break;
			}
			return next;
		}
		case std::uint8_t(node_type::variable_10):
		case std::uint8_t(node_type::variable):
			out = read_variable(variable(self, n.param));
			return next;
		case std::uint8_t(node_type::integer): out = value::integer(std::int32_t(n.param)); return next;
		case std::uint8_t(node_type::real): out = value::of(value_type::real, n.param); return next;
		case std::uint8_t(node_type::input): out = value::of(value_type::input, n.param); return next;
		case std::uint8_t(node_type::constant_vector):
			out = value::vector(m_memory.f32(n.param), m_memory.f32(n.param + 4), m_memory.f32(n.param + 8));
			return next;
		case std::uint8_t(node_type::vector):
		{
			value x, y, z;
			next = eval(self, next, x);
			next = eval(self, next, y);
			next = eval(self, next, z);
			out = value::vector(x.as_float(), y.as_float(), z.as_float());
			return next;
		}
		case std::uint8_t(node_type::mask): out = value::of(value_type::mask, n.param & 0xFFFF); return next;
		case std::uint8_t(node_type::small_id): case 29: out = value::of(0x16, n.param); return next;
		case 19: out = value::of(0x17, n.param); return next;
		case std::uint8_t(node_type::text): out = value::of(value_type::text, n.param); return next;
		case 21: out = value::of(0x1C, n.param); return next;
		case std::uint8_t(node_type::family): out = value::of(value_type::family, n.param); return next;
		case std::uint8_t(node_type::actor): out = value::of(value_type::actor, n.param); return next;
		case std::uint8_t(node_type::state): out = value::of(value_type::state, n.param); return next;
		case std::uint8_t(node_type::super_object): out = value::of(value_type::super_object, n.param); return next;
		case std::uint8_t(node_type::waypoint): out = value::of(value_type::waypoint, n.param); return next;
		case std::uint8_t(node_type::id_27): out = value::of(value_type::id, n.param); return next;
		case std::uint8_t(node_type::behaviour): out = value::of(value_type::behaviour, n.param); return next;
		case std::uint8_t(node_type::id_30): out = value::of(0x21, n.param); return next;
		case std::uint8_t(node_type::reference_31): out = value::of(0x1D, n.param); return next;
		case std::uint8_t(node_type::reference_32): out = value::of(0x26, n.param); return next;
		case 33: out = value::of(0x25, n.param); return next;
		case 34: out = value::of(0x27, n.param); return next;
		case std::uint8_t(node_type::model): out = value::of(value_type::model, n.param); return next;
		case std::uint8_t(node_type::reference_36): out = value::of(0x2A, n.param); return next;
		case std::uint8_t(node_type::flags_37): out = value::of(0x2B, n.param); return next;
		case std::uint8_t(node_type::reference_38): out = value::of(0x2C, n.param); return next;
		case std::uint8_t(node_type::subroutine): return subroutine(self, at, out);
		case std::uint8_t(node_type::ignored): return next;
		default:
			throw format_error(std::format("script node at 0x{:08X} has unknown type {}", at, n.type));
		}
	}

	// --- Keywords (0x4A5E80) -------------------------------------------------------------------

	std::uint32_t interpreter::keyword(std::uint32_t self, std::uint32_t at, value& out)
	{
		node_view n = node(at);
		std::uint32_t next = at + node_size;
		out = value::of(value_type::nothing, 0);
		// The node after a block: the next one at this indent or shallower. The exe caches the
		// distance in the node (+4); the result is the same.
		auto after_block = [&](std::uint32_t from) { return skip(from); };
		switch(n.param)
		{
		case 0: case 1: case 2: case 3: case 4: case 5:
		{
			// if / if not / if checked every 2, 4, 8, 16 frames (0x489250).
			static constexpr std::uint8_t masks[] = { 0, 0, 1, 3, 7, 15 };
			bool truth = false;
			if(n.param >= 2 && (m_frame & masks[n.param]) != 0)
			{
				// Not this frame: like a false condition.
				next = skip(at);
			}
			else
			{
				value condition;
				next = eval(self, next, condition);
				truth = condition.word() != 0;
				if(n.param == 1)
					truth = !truth;
			}
			if(!truth)
			{
				// Skip the "then" block that follows, landing on "else" if there is one.
				node_view then = node(next);
				if(then.type != std::uint8_t(node_type::end))
				{
					if(then.type == std::uint8_t(node_type::keyword) && then.param == 8 && then.indent == n.indent)
						next = after_block(next);
				}
				node_view after = node(next);
				if(after.indent == n.indent && after.type == std::uint8_t(node_type::keyword) && after.param == 9)
					next += node_size; // into the else block
			}
			out = value::of(value_type::nothing, 0);
			return next;
		}
		case 6: case 7:
			// The statements one indent deeper run unless a global switch is on; both are off in the
			// release game (0x4A66BC, 0x4B9C34), so they always run.
			return next;
		case 8:
			// then: run the block (0x489150).
			return block(self, next, out);
		case 9:
			// else, reached after a "then" ran: skip the else block (0x489170).
			return after_block(at);
		case 10:
		{
			// jump by a computed number of nodes (0x489340).
			value distance;
			std::uint32_t after = eval(self, next, distance);
			std::int32_t d = distance.as_int();
			out = value::of(value_type::nothing, 0);
			return d ? after + std::uint32_t(d) * node_size : after;
		}
		case 11: out = value::of(value_type::actor, actor_of(self)); return next;
		case 12: out = value::of(value_type::actor, actor_of(m_main_actor)); return next;
		case 13: out = value::of(value_type::actor, 0); return next;
		case 14: out = value::of(value_type::super_object, 0); return next;
		case 15: out = value::of(value_type::waypoint, 0); return next;
		case 16: out = value::of(value_type::id, 0xFFFFFFFF); return next;
		case 17: out = value::of(0x2B, 0); return next;
		case 18: out = value::of(0x2C, 0); return next;
		case 19: out = value::of(value_type::state, 0); return next;
		default:
			throw format_error(std::format("unknown keyword {}", n.param));
		}
	}

	// --- Variables -----------------------------------------------------------------------------

	interpreter::variable_slot interpreter::variable(std::uint32_t self, std::uint32_t index) const
	{
		// An actor without a brain (or none at all) has no variables: address 0 reads as 0.
		std::uint32_t actor = actor_of(self);
		std::uint32_t brain = actor ? m_memory.u32(actor + actor_field::brain) : 0;
		std::uint32_t mind = brain && m_memory.contains(brain, 4) ? m_memory.u32(brain) : 0;
		if(!mind)
			return {};
		std::uint32_t variables = m_memory.u32(mind + mind_field::variables);
		std::uint32_t declaration = m_memory.u32(m_memory.u32(variables));
		std::uint32_t info = m_memory.u32(declaration + declaration_info) + (index & 0xFF) * variable_info_size;
		return { m_memory.u32(variables + variables_current) + m_memory.u32(info), m_memory.u32(info + 4) };
	}

	value interpreter::read_variable(const variable_slot& s) const
	{
		std::uint32_t a = s.address;
		if(!a)
			return value::integer(0);
		switch(s.type)
		{
		case 0: case 1: return value::integer(std::int8_t(m_memory.u8(a)));
		case 2: return value::integer(m_memory.u8(a));
		case 3: return value::integer(m_memory.read<std::int16_t>(a));
		case 4: return value::integer(m_memory.read<std::uint16_t>(a));
		case 5: case 6: return value::integer(std::int32_t(m_memory.u32(a)));
		case 7: return value::real(m_memory.f32(a));
		case 8: return value::of(value_type::waypoint, m_memory.u32(a));
		case 9: return value::of(value_type::actor, m_memory.u32(a));
		case 10: return value::of(value_type::buffer, a);
		case 11: return value::vector(m_memory.f32(a), m_memory.f32(a + 4), m_memory.f32(a + 8));
		case 12: return value::of(value_type::behaviour, m_memory.u32(a));
		case 13: return value::of(value_type::state, m_memory.u32(a));
		case 14: return value::of(value_type::id, m_memory.u32(a));
		case 15: return value::of(0x26, m_memory.u32(a));
		case 16: return value::of(0x2B, m_memory.u32(a));
		case 17: return value::of(0x2C, m_memory.u32(a));
		case 24: return value::of(value_type::super_object, m_memory.u32(a));
		default: return value::of(value_type::buffer, a); // arrays: their address
		}
	}

	void interpreter::write_variable(const variable_slot& s, const value& v)
	{
		std::uint32_t a = s.address;
		if(!a)
			return;
		switch(s.type)
		{
		case 0: case 1: case 2: m_memory.write<std::uint8_t>(a, std::uint8_t(v.as_int())); break;
		case 3: case 4: m_memory.write<std::uint16_t>(a, std::uint16_t(v.as_int())); break;
		case 5: case 6: m_memory.write<std::int32_t>(a, v.as_int()); break;
		case 7: m_memory.write<float>(a, v.as_float()); break;
		case 11:
			for(int i = 0; i < 3; i++)
				m_memory.write<float>(a + std::uint32_t(i) * 4, v.component(i));
			break;
		case 8: case 9: case 12: case 13: case 14: case 15: case 16: case 17: case 24:
			m_memory.write<std::uint32_t>(a, v.word());
			break;
		default:
			break; // whole arrays aren't assigned
		}
	}

	// --- Operators (0x4A6398) ------------------------------------------------------------------

	std::uint32_t interpreter::operation(std::uint32_t self, std::uint32_t at, value& out)
	{
		node_view n = node(at);
		std::uint32_t next = at + node_size;
		switch(n.param)
		{
		case 0: case 1: case 2: case 3: case 16: case 17: case 18: case 19:
		{
			value a, b;
			next = eval(self, next, a);
			next = eval(self, next, b);
			int op = int(n.param % 16);
			if(a.type == value_type::vector)
			{
				float s = b.type == value_type::vector ? 0 : b.as_float();
				std::array<float, 3> r{};
				for(int i = 0; i < 3; i++)
				{
					float x = a.component(i), y = b.type == value_type::vector ? b.component(i) : s;
					r[std::size_t(i)] = op == 0 ? x + y : op == 1 ? x - y : op == 2 ? x * y : (y != 0 ? x / y : x);
				}
				out = value::vector(r[0], r[1], r[2]);
			}
			else if(is_float_math(a, b))
			{
				float x = a.as_float(), y = b.as_float();
				if(op == 3 && std::abs(y) < 1e-6f)
					out = a; // the exe leaves the left side when dividing by (nearly) zero
				else
					out = value::real(op == 0 ? x + y : op == 1 ? x - y : op == 2 ? x * y : x / y);
			}
			else
			{
				std::int32_t x = a.as_int(), y = b.as_int();
				if(op == 3 && y == 0)
					out = a;
				else
					out = value::integer(op == 0 ? x + y : op == 1 ? x - y : op == 2 ? x * y : x / y);
			}
			return next;
		}
		case 4: case 20:
		{
			value a;
			next = eval(self, next, a);
			if(a.type == value_type::vector)
				out = value::vector(-a.component(0), -a.component(1), -a.component(2));
			else
				out = a.is_real() ? value::real(-a.as_float()) : value::integer(-a.as_int());
			return next;
		}
		case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 21: case 22: case 23: case 27:
			return assignment(self, at, n.param, out);
		case 12:
		{
			// another actor's variable
			value who;
			next = eval(self, next, who);
			std::uint32_t other = who.type == value_type::actor ? super_object_of(who.word()) : who.word();
			node_view var = node(next);
			out = other ? read_variable(variable(other, var.param)) : value::integer(0);
			return next + node_size;
		}
		case 13: case 14: case 15:
		{
			value v;
			next = eval(self, next, v);
			out = value::real(v.component(int(n.param - 13)));
			return next;
		}
		case 24:
		{
			// "actor: call": the call acts on that actor (0x4825E0).
			value who;
			next = eval(self, next, who);
			std::uint32_t other = super_object_of(who.word());
			m_acting = other != self ? other : 0;
			return eval(self, next, out);
		}
		case 25:
		{
			// an actor seen as having a given model: just the actor (0x482620)
			value model, who;
			next = eval(self, next, model);
			next = eval(self, next, who);
			out = value::of(value_type::actor, who.word());
			return next;
		}
		case 26:
		{
			// array element: variable[index]
			place p;
			next = resolve_place(self, at, p);
			out = read_place(self, p);
			return next;
		}
		default:
			throw format_error(std::format("unknown operator {}", n.param));
		}
	}

	std::uint32_t interpreter::resolve_place(std::uint32_t self, std::uint32_t at, place& p)
	{
		node_view n = node(at);
		std::uint32_t next = at + node_size;
		if(n.type == std::uint8_t(node_type::variable) || n.type == std::uint8_t(node_type::variable_10))
		{
			p.what = place::kind::variable;
			p.slot = variable(self, n.param);
			return next;
		}
		if(n.type == std::uint8_t(node_type::field))
		{
			p.what = place::kind::field;
			p.field = n.param;
			p.actor = self;
			return next;
		}
		if(n.type == std::uint8_t(node_type::operator_))
		{
			switch(n.param)
			{
			case 12:
			{
				value who;
				next = eval(self, next, who);
				std::uint32_t other = who.type == value_type::actor ? super_object_of(who.word()) : who.word();
				node_view var = node(next);
				p.what = place::kind::variable;
				p.slot = variable(other ? other : self, var.param);
				return next + node_size;
			}
			case 13: case 14: case 15:
			{
				next = resolve_place(self, next, p);
				p.what = place::kind::component;
				p.component = int(n.param - 13);
				return next;
			}
			case 26:
			{
				next = resolve_place(self, next, p);
				value index;
				next = eval(self, next, index);
				p.what = place::kind::element;
				p.element = std::uint32_t(index.as_int());
				return next;
			}
			default:
				break;
			}
		}
		throw format_error(std::format("script node at 0x{:08X} can't be assigned to", at));
	}

	value interpreter::read_place(std::uint32_t, const place& p)
	{
		switch(p.what)
		{
		case place::kind::variable:
			return read_variable(p.slot);
		case place::kind::component:
			return value::real(read_variable(p.slot).component(p.component));
		case place::kind::element:
		{
			// An array: u32 element type, u8 count, elements from +8 (vectors take 12 bytes).
			std::uint32_t array = p.slot.address;
			std::uint32_t type = m_memory.u32(array);
			std::uint32_t size = type == 11 ? 12 : 4;
			return read_variable({ array + 8 + p.element * size, type });
		}
		case place::kind::field:
		{
			value v;
			// Reuse the field reader through a fake node is overkill; fields are read by eval.
			return v;
		}
		}
		return {};
	}

	void interpreter::write_place(std::uint32_t self, const place& p, const value& v)
	{
		switch(p.what)
		{
		case place::kind::variable:
			write_variable(p.slot, v);
			break;
		case place::kind::component:
		{
			value whole = read_variable(p.slot);
			whole.words[std::size_t(p.component)] = std::bit_cast<std::uint32_t>(v.as_float());
			write_variable(p.slot, whole);
			break;
		}
		case place::kind::element:
		{
			std::uint32_t array = p.slot.address;
			std::uint32_t type = m_memory.u32(array);
			std::uint32_t size = type == 11 ? 12 : 4;
			write_variable({ array + 8 + p.element * size, type }, v);
			break;
		}
		case place::kind::field:
		{
			std::uint32_t actor = actor_of(p.actor);
			switch(p.field)
			{
			case 9: case 10: m_memory.write<float>(m_memory.u32(actor) + data_3d_field::shadow_size + (p.field - 9) * 4, v.as_float()); break;
			case 11: g_registers.vector = { v.component(0), v.component(1), v.component(2) }; break;
			case 12: g_registers.short_a = std::int16_t(v.as_int()); break;
			case 13: g_registers.short_b = std::int16_t(v.as_int()); break;
			case 14: g_registers.real_a = v.as_float(); break;
			case 15: g_registers.real_b = v.as_float(); break;
			case 16: g_registers.real_c = v.as_float(); break;
			case 17: g_registers.integer = v.as_int(); break;
			default: break; // the others are read-only
			}
			(void)self;
			break;
		}
		}
	}

	std::uint32_t interpreter::assignment(std::uint32_t self, std::uint32_t at, std::uint32_t op, value& out)
	{
		place p;
		std::uint32_t next = at + node_size;
		node_view target_node = node(next);
		if(op >= 21 && op <= 23)
		{
			// target.x := value
			next = resolve_place(self, next, p);
			p.what = place::kind::component;
			p.component = int(op - 21);
		}
		else
			next = resolve_place(self, next, p);

		value current;
		if(p.what == place::kind::field)
		{
			// Fields are read by evaluating the field node itself.
			eval(self, at + node_size, current);
		}
		else
			current = read_place(self, p);
		(void)target_node;

		value rhs;
		if(op != 9 && op != 10)
			next = eval(self, next, rhs);

		value result;
		switch(op)
		{
		case 11: case 21: case 22: case 23: case 27:
			result = rhs;
			break;
		case 9: case 10:
			result = current.is_real() ? value::real(current.as_float() + (op == 9 ? 1.0f : -1.0f))
				: value::integer(current.as_int() + (op == 9 ? 1 : -1));
			break;
		default:
		{
			int kind = int(op - 5); // += -= *= /=
			if(current.type == value_type::vector)
			{
				std::array<float, 3> r{};
				for(int i = 0; i < 3; i++)
				{
					float x = current.component(i);
					float y = rhs.type == value_type::vector ? rhs.component(i) : rhs.as_float();
					r[std::size_t(i)] = kind == 0 ? x + y : kind == 1 ? x - y : kind == 2 ? x * y : (y != 0 ? x / y : x);
				}
				result = value::vector(r[0], r[1], r[2]);
			}
			else if(current.is_real() || rhs.is_real() || p.what == place::kind::component)
			{
				float x = current.as_float(), y = rhs.as_float();
				result = value::real(kind == 0 ? x + y : kind == 1 ? x - y : kind == 2 ? x * y : (y != 0 ? x / y : x));
			}
			else
			{
				std::int32_t x = current.as_int(), y = rhs.as_int();
				result = value::integer(kind == 0 ? x + y : kind == 1 ? x - y : kind == 2 ? x * y : (y != 0 ? x / y : x));
			}
			break;
		}
		}
		write_place(self, p, result);
		out = value::of(value_type::nothing, 0);
		return next;
	}

	// --- Blocks and behaviours -----------------------------------------------------------------

	std::uint8_t interpreter::settle(const value& out, std::uint32_t next, std::uint8_t indent)
	{
		std::uint8_t flags = std::uint8_t(out.words[0]);
		if(m_signal & flow::no_signal)
			return flags;
		if(m_pending)
		{
			m_pending = false;
			return flags;
		}
		if(!((flags & flow::done) && node(next).indent == indent))
			flags = std::uint8_t((flags & 0x1C) | m_signal);
		m_signal = flow::no_signal;
		m_pending = true;
		return flags;
	}

	std::uint32_t interpreter::block(std::uint32_t self, std::uint32_t at, value& out)
	{
		std::uint8_t indent = node(at).indent;
		std::uint32_t next = eval(self, at, out);
		std::uint8_t flags = settle(out, next, indent);
		while(!(flags & (flow::end | flow::restart)))
		{
			if(flags & flow::done)
				pop_action(m_intelligence);
			if(flags & flow::stop)
			{
				drop_actions_after(m_intelligence, m_script);
				out = value::of(value_type::nothing, flags);
				return next;
			}
			if(node(next).type == std::uint8_t(node_type::end))
			{
				flags = flow::end;
				break;
			}
			if(flags & flow::wait)
				break;
			if(m_schedule)
				m_memory.write<std::uint32_t>(m_intelligence + intelligence_field::schedule_at, next);
			indent = node(next).indent;
			std::uint32_t after = eval(self, next, out);
			flags = settle(out, after, indent);
			next = after;
		}
		out = value::of(value_type::nothing, flags);
		return next;
	}

	std::uint8_t interpreter::run_rules(std::uint32_t self, std::uint32_t intelligence)
	{
		m_schedule = false;
		m_intelligence = intelligence;
		for(;;)
		{
			std::uint32_t behaviour = m_memory.u32(intelligence + intelligence_field::behaviour);
			std::uint8_t count = m_memory.u8(behaviour + behaviour_field::rule_count);
			std::uint32_t rules = m_memory.u32(behaviour + behaviour_field::rules);
			std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
			std::uint8_t capacity = actions ? m_memory.u8(actions + actions_field::capacity) : 0;
			bool restarted = false;
			for(std::uint8_t i = 0; i < count; i++)
			{
				m_script = std::uint8_t(i + 1);
				std::uint8_t entry = find_action(intelligence, m_script);
				std::uint32_t start = entry < capacity ? resume_action(intelligence, entry) : m_memory.u32(rules + i * 4u);
				value out;
				block(self, start, out);
				std::uint8_t flags = std::uint8_t(out.word());
				if(flags & flow::stop)
					return 1;
				if(flags & flow::restart)
				{
					m_acted = false;
					restarted = true;
					break;
				}
				if(!(flags & flow::end))
					return 0;
			}
			if(!restarted)
				return 0;
		}
	}

	std::uint8_t interpreter::run_schedule(std::uint32_t self, std::uint32_t intelligence)
	{
		m_schedule = true;
		m_intelligence = intelligence;
		std::uint32_t at = m_memory.u32(intelligence + intelligence_field::schedule_at);
		if(!at)
			return flow::end;
		m_script = 0;
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		std::uint8_t capacity = actions ? m_memory.u8(actions + actions_field::capacity) : 0;
		std::uint8_t entry = find_action(intelligence, 0);
		if(entry < capacity)
			at = resume_action(intelligence, entry);
		if(node(at).type == std::uint8_t(node_type::end))
		{
			// Finished last time: start over.
			std::uint32_t behaviour = m_memory.u32(intelligence + intelligence_field::behaviour);
			at = m_memory.u32(m_memory.u32(behaviour + behaviour_field::schedule));
		}
		value out;
		std::uint32_t next = block(self, at, out);
		std::uint8_t flags = std::uint8_t(out.word());
		if(!(flags & (flow::restart | flow::wait)))
			m_memory.write<std::uint32_t>(intelligence + intelligence_field::schedule_at, next);
		return flags;
	}

	void interpreter::reset_schedule(std::uint32_t intelligence)
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		std::uint8_t capacity = actions ? m_memory.u8(actions + actions_field::capacity) : 0;
		std::uint8_t entry = find_action(intelligence, 0);
		if(entry < capacity)
		{
			m_memory.write<std::uint32_t>(intelligence + intelligence_field::schedule_at, resume_action(intelligence, entry));
			drop_schedule_actions(intelligence);
		}
	}

	bool interpreter::run(std::uint32_t self, std::uint32_t intelligence)
	{
		bool acted = false;
		std::uint8_t flags = 0;
		do
		{
			m_acted = false;
			flags = run_rules(self, intelligence);
			if(!(flags & flow::restart))
			{
				if(!(flags & flow::stop) && !m_acted)
					flags = run_schedule(self, intelligence);
				else
					reset_schedule(intelligence);
			}
			acted = acted || m_acted;
		} while((flags & flow::restart) && !(flags & flow::stop));
		return acted;
	}

	void interpreter::change_behaviour(std::uint32_t intelligence, std::uint32_t behaviour, bool other)
	{
		// 0x469600, 0x481080
		if(m_memory.u32(intelligence + intelligence_field::behaviour) == behaviour)
			return;
		m_memory.write<std::uint32_t>(intelligence + intelligence_field::previous, m_memory.u32(intelligence + intelligence_field::behaviour));
		m_memory.write<std::uint32_t>(intelligence + intelligence_field::behaviour, behaviour);
		clear_actions(intelligence);
		std::uint32_t schedule = m_memory.u32(behaviour + behaviour_field::schedule);
		m_memory.write<std::uint32_t>(intelligence + intelligence_field::schedule_at, schedule ? m_memory.u32(schedule) : 0);
		if(!other)
			signal(flow::restart);
	}

	void interpreter::think(std::uint32_t self)
	{
		std::uint32_t actor = actor_of(self);
		if(!actor || removed(self))
			return;
		std::uint32_t data = m_memory.u32(actor + actor_field::data_3d);
		if(data && honour_brain_period)
		{
			// 0x418880, 0x4188C0: think on every (period + 1)th frame.
			std::int8_t counter = std::int8_t(m_memory.u8(data + data_3d_field::brain_counter) + 1);
			if(counter >= std::int8_t(m_memory.u8(data + data_3d_field::brain_period)))
				counter = 0;
			m_memory.write<std::int8_t>(data + data_3d_field::brain_counter, counter);
			if(counter != 0)
				return;
		}
		std::uint32_t brain = m_memory.u32(actor + actor_field::brain);
		if(!brain || !m_memory.contains(brain, 4))
			return;
		std::uint32_t mind = m_memory.u32(brain);
		if(!mind || !m_memory.u32(mind + mind_field::model))
			return;
		if(data)
		{
			std::uint16_t frame = std::uint16_t(m_memory.read<std::uint16_t>(data + data_3d_field::frame) + 1);
			m_memory.write<std::uint16_t>(data + data_3d_field::frame, frame);
			m_frame = std::uint8_t(frame);
		}
		else
			m_frame = 0;

		m_acting = 0;
		bool reflex_took_over = false;
		if(std::uint32_t reflex = m_memory.u32(mind + mind_field::reflex))
		{
			m_memory.write<std::uint8_t>(mind + mind_field::running_normal, 0);
			reflex_took_over = run(self, reflex);
		}
		if(!reflex_took_over)
		{
			if(std::uint32_t normal = m_memory.u32(mind + mind_field::normal))
			{
				m_memory.write<std::uint8_t>(mind + mind_field::running_normal, 1);
				run(self, normal);
			}
		}
	}

	// --- Macros (0x480220) ---------------------------------------------------------------------

	std::uint32_t interpreter::subroutine(std::uint32_t self, std::uint32_t at, value& out)
	{
		std::uint32_t macro = node(at).param;
		std::uint32_t saved = m_memory.u32(macro), running = m_memory.u32(macro + 4);
		auto reset = [&] { m_memory.write<std::uint32_t>(running, m_memory.u32(saved)); };
		m_macro_depth++;
		std::uint32_t outer = m_macro_node;
		m_macro_node = at;

		std::uint32_t actions = m_memory.u32(m_intelligence + intelligence_field::actions);
		std::uint8_t capacity = actions ? m_memory.u8(actions + actions_field::capacity) : 0;
		std::uint8_t entry = find_action(m_intelligence, m_script);
		if(entry >= capacity)
			reset();
		else if(resume_action(m_intelligence, entry) != m_macro_node)
			reset();

		value result;
		block(self, m_memory.u32(running), result);
		m_macro_depth--;
		std::uint8_t flags = std::uint8_t(result.word());
		if(flags & (flow::end | flow::restart))
		{
			reset();
			out = flags & flow::end ? value::of(value_type::nothing, 0) : result;
			m_macro_node = outer;
			return at + node_size;
		}
		// Waiting inside the macro: remember where, and come back to this node.
		entry = find_action(m_intelligence, m_script);
		if(entry < capacity)
		{
			std::uint32_t where = resume_action(m_intelligence, entry);
			if(node(where).type != std::uint8_t(node_type::subroutine))
				m_memory.write<std::uint32_t>(running, where);
			pop_action(m_intelligence);
			push_action(m_intelligence, m_script, m_macro_node);
		}
		out = result;
		m_macro_node = outer;
		return at;
	}

	// --- Meta-actions (0x480A50) ---------------------------------------------------------------

	std::uint32_t interpreter::meta_action(std::uint32_t self, std::uint32_t at, value& out)
	{
		std::uint32_t index = node(at).param;
		std::uint32_t key = m_macro_depth ? m_macro_node : at;
		std::uint32_t actions = m_memory.u32(m_intelligence + intelligence_field::actions);
		if(!actions)
		{
			out = value::of(value_type::nothing, 0);
			return skip(at);
		}
		std::uint8_t capacity = m_memory.u8(actions + actions_field::capacity);
		auto h = m_metas.find(index);
		std::uint8_t entry = find_action(m_intelligence, m_script, key);
		m_memory.write<std::uint8_t>(actions + actions_field::current, entry);
		auto scratch = [&](std::uint8_t e) { return m_memory.u32(actions + actions_field::entries) + e * action_field::size; };
		if(entry < capacity)
		{
			// Resuming: check whether it's done.
			std::uint32_t next = skip(at);
			std::uint8_t flags = h == m_metas.end() ? 0 : h->second.check(*this, self, scratch(entry));
			out = value::of(value_type::nothing, flags);
			m_acted = true;
			return next;
		}
		drop_actions_after(m_intelligence, m_script);
		entry = free_action(m_intelligence);
		m_memory.write<std::uint8_t>(actions + actions_field::current, entry);
		if(entry >= capacity)
		{
			out = value::of(value_type::nothing, 0);
			return skip(at);
		}
		push_action(m_intelligence, m_script, at);
		m_acted = true;
		call c{ *this, self, at + node_size, index };
		if(h == m_metas.end())
		{
			note_missing("meta_action", index);
			out = value::of(value_type::nothing, 0);
			return skip(at);
		}
		std::uint32_t next = h->second.start(c, scratch(entry));
		out = value::of(value_type::nothing, h->second.check(*this, self, scratch(entry)));
		return next;
	}

	// --- The table of actions in progress ------------------------------------------------------

	std::uint8_t interpreter::find_action(std::uint32_t intelligence, std::uint8_t script) const
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		if(!actions)
			return 0;
		std::uint8_t capacity = m_memory.u8(actions + actions_field::capacity);
		if(!m_memory.u8(actions + actions_field::used))
			return capacity;
		std::uint32_t entries = m_memory.u32(actions + actions_field::entries);
		for(std::uint8_t i = 0; i < capacity; i++)
		{
			std::uint32_t e = entries + i * action_field::size;
			if(m_memory.u8(e + action_field::used) && m_memory.u8(e + action_field::script) == script)
				return i;
		}
		return capacity;
	}

	std::uint8_t interpreter::find_action(std::uint32_t intelligence, std::uint8_t script, std::uint32_t key) const
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		std::uint8_t capacity = m_memory.u8(actions + actions_field::capacity);
		if(!m_memory.u8(actions + actions_field::used))
			return capacity;
		std::uint32_t entries = m_memory.u32(actions + actions_field::entries);
		for(std::uint8_t i = 0; i < capacity; i++)
		{
			std::uint32_t e = entries + i * action_field::size;
			if(m_memory.u8(e + action_field::used) && m_memory.u8(e + action_field::script) == script && m_memory.u32(e + action_field::node) == key)
				return i;
		}
		return capacity;
	}

	std::uint8_t interpreter::free_action(std::uint32_t intelligence) const
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		std::uint8_t capacity = m_memory.u8(actions + actions_field::capacity);
		if(m_memory.u8(actions + actions_field::used) >= capacity)
			return capacity;
		std::uint32_t entries = m_memory.u32(actions + actions_field::entries);
		for(std::uint8_t i = 0; i < capacity; i++)
			if(!m_memory.u8(entries + i * action_field::size + action_field::used))
				return i;
		return capacity;
	}

	void interpreter::write_action(std::uint32_t intelligence, std::uint8_t entry, std::uint8_t script, std::uint32_t node_at, bool used)
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		std::uint32_t e = m_memory.u32(actions + actions_field::entries) + entry * action_field::size;
		m_memory.write<std::uint8_t>(e + action_field::pending, m_pending ? 1 : 0);
		m_memory.write<std::uint8_t>(e + action_field::signal, m_signal);
		m_memory.write<std::uint8_t>(e + action_field::used, used ? 1 : 0);
		m_memory.write<std::uint32_t>(e + action_field::node, node_at);
		m_memory.write<std::uint8_t>(e + action_field::script, script);
		auto scratch = m_memory.span(e, action_field::scratch);
		std::fill(scratch.begin(), scratch.end(), std::byte(0));
	}

	void interpreter::push_action(std::uint32_t intelligence, std::uint8_t script, std::uint32_t node_at)
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		std::uint8_t current = m_memory.u8(actions + actions_field::current);
		std::uint32_t e = m_memory.u32(actions + actions_field::entries) + current * action_field::size;
		if(m_memory.u8(e + action_field::used))
			return;
		write_action(intelligence, current, script, node_at, true);
		m_memory.write<std::uint8_t>(actions + actions_field::used, std::uint8_t(m_memory.u8(actions + actions_field::used) + 1));
	}

	void interpreter::pop_action(std::uint32_t intelligence)
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		if(!actions)
			return;
		std::uint8_t current = m_memory.u8(actions + actions_field::current);
		std::uint32_t e = m_memory.u32(actions + actions_field::entries) + current * action_field::size;
		if(!m_memory.u8(e + action_field::used))
			return;
		write_action(intelligence, current, 0, 0, false);
		m_memory.write<std::uint8_t>(actions + actions_field::used, std::uint8_t(m_memory.u8(actions + actions_field::used) - 1));
	}

	void interpreter::drop_actions_after(std::uint32_t intelligence, std::uint8_t script)
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		if(!actions || !m_memory.u8(actions + actions_field::used))
			return;
		std::uint8_t capacity = m_memory.u8(actions + actions_field::capacity);
		std::uint32_t entries = m_memory.u32(actions + actions_field::entries);
		for(std::uint8_t i = 0; i < capacity; i++)
		{
			std::uint32_t e = entries + i * action_field::size;
			if(m_memory.u8(e + action_field::used) && m_memory.u8(e + action_field::script) > script)
			{
				write_action(intelligence, i, 0, 0, false);
				m_memory.write<std::uint8_t>(actions + actions_field::used, std::uint8_t(m_memory.u8(actions + actions_field::used) - 1));
			}
		}
	}

	void interpreter::drop_schedule_actions(std::uint32_t intelligence)
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		if(!actions || !m_memory.u8(actions + actions_field::used))
			return;
		std::uint8_t capacity = m_memory.u8(actions + actions_field::capacity);
		std::uint32_t entries = m_memory.u32(actions + actions_field::entries);
		for(std::uint8_t i = 0; i < capacity; i++)
		{
			std::uint32_t e = entries + i * action_field::size;
			if(m_memory.u8(e + action_field::used) && m_memory.u8(e + action_field::script) == 0)
			{
				write_action(intelligence, i, 0, 0, false);
				m_memory.write<std::uint8_t>(actions + actions_field::used, std::uint8_t(m_memory.u8(actions + actions_field::used) - 1));
			}
		}
	}

	void interpreter::clear_actions(std::uint32_t intelligence)
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		if(!actions)
			return;
		std::uint8_t capacity = m_memory.u8(actions + actions_field::capacity);
		for(std::uint8_t i = 0; i < capacity; i++)
			write_action(intelligence, i, 0, 0, false);
		m_memory.write<std::uint8_t>(actions + actions_field::used, 0);
	}

	std::uint32_t interpreter::resume_action(std::uint32_t intelligence, std::uint8_t entry)
	{
		std::uint32_t actions = m_memory.u32(intelligence + intelligence_field::actions);
		std::uint32_t e = m_memory.u32(actions + actions_field::entries) + entry * action_field::size;
		m_pending = m_memory.u8(e + action_field::pending) != 0;
		m_signal = m_memory.u8(e + action_field::signal);
		return m_memory.u32(e + action_field::node);
	}
}
