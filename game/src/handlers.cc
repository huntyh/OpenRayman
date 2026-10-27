#include <openrayman/game/handlers.h>

#include <bit>
#include <cmath>
#include <cstdlib>

// The conditions, functions and procedures scripts call, each written from what its handler in
// Rayman2.exe does (the address is given with each). Names are ours. See docs/formats/scripts.md.

namespace openrayman::game
{
	namespace
	{
		namespace actor_field
		{
			constexpr std::uint32_t data_3d = 0x00, standard_game = 0x04, brain = 0x0C, collision = 0x14;
		}
		namespace standard_game_field
		{
			constexpr std::uint32_t hit_points = 0x21, max_hit_points = 0x22, top_hit_points = 0x23; // u8
			constexpr std::uint32_t custom_bits = 0x24; // u32
			constexpr std::uint32_t flags = 0x29;       // u8
		}
		namespace data_3d_field
		{
			constexpr std::uint32_t state = 0x08, family = 0x14; // the state being played
		}
		namespace mind_field
		{
			constexpr std::uint32_t model = 0x00, normal = 0x04, reflex = 0x08, running_normal = 0x10;
		}
		constexpr std::uint32_t intelligence_behaviour = 0x08;
		constexpr std::uint32_t super_object_transform = 0x24; // global: +4 position, +0x10.. axes
		constexpr std::uint32_t collision_event = 0x65;        // u8, in the actor's collision structure

		// A pointer field of a super object's actor, or 0 when there is no such actor (scripts may
		// name actors whose references aren't set while handlers are missing).
		std::uint32_t actor_part(interpreter& vm, std::uint32_t super_object, std::uint32_t field)
		{
			std::uint32_t actor = vm.actor_of(super_object);
			return actor && vm.memory().contains(actor + field, 4) ? vm.memory().u32(actor + field) : 0;
		}

		std::uint32_t mind_of(interpreter& vm, std::uint32_t super_object)
		{
			live_memory& m = vm.memory();
			std::uint32_t actor = vm.actor_of(super_object);
			std::uint32_t brain = actor ? m.u32(actor + actor_field::brain) : 0;
			return brain ? m.u32(brain) : 0;
		}

		// An argument meant as an actor: actors and super objects both name one.
		std::uint32_t actor_value(interpreter& vm, const value& v)
		{
			if(v.type == value_type::super_object)
				return vm.actor_of(v.word());
			return v.word();
		}

		std::uint32_t next_arg(call& c, value& v)
		{
			c.args = c.vm.eval(c.self, c.args, v);
			return c.args;
		}

		// --- Conditions --------------------------------------------------------------------------

		// 0..3 (0x489B40): and, or (short-circuit: the second argument is skipped when it can't
		// matter), not, and an "xor" that compares its first argument with itself (always false).
		std::uint32_t logic(call& c, bool& result)
		{
			value a;
			next_arg(c, a);
			bool x = a.word() != 0;
			switch(c.index)
			{
			case 0:
				if(!x)
				{
					result = false;
					return c.vm.skip(c.args);
				}
				break;
			case 1:
				if(x)
				{
					result = true;
					return c.vm.skip(c.args);
				}
				break;
			case 2:
				result = !x;
				return c.args;
			case 3:
				result = false;
				return c.args;
			}
			value b;
			next_arg(c, b);
			result = b.word() != 0;
			return c.args;
		}

		// 4..9 (0x489830): ==, !=, <, >, <=, >=. Integers and floats mix as floats; vectors only
		// compare for (in)equality.
		std::uint32_t compare(call& c, bool& result)
		{
			value a, b;
			next_arg(c, a);
			next_arg(c, b);
			int op = int(c.index - 4);
			result = false;
			if(a.type == value_type::vector || b.type == value_type::vector)
			{
				if(a.type != b.type)
					return c.args;
				bool same = a.words == b.words;
				result = op == 0 ? same : op == 1 ? !same : false;
				return c.args;
			}
			bool numbers = (a.type == value_type::integer || a.type == value_type::real) && (b.type == value_type::integer || b.type == value_type::real);
			if(numbers && (a.is_real() || b.is_real()))
			{
				float x = a.as_float(), y = b.as_float();
				bool r[] = { x == y, x != y, x < y, x > y, x <= y, x >= y };
				result = r[op];
			}
			else if(numbers || a.type == b.type)
			{
				// Integers, or two references of one kind (compared by address for (in)equality).
				std::int32_t x = std::int32_t(a.word()), y = std::int32_t(b.word());
				bool r[] = { x == y, x != y, x < y, x > y, x <= y, x >= y };
				result = r[op];
			}
			return c.args;
		}

		// 44..47 (0x489C80): an input action's "frames held" counter (+0xC): held, pressed this
		// frame, released, not held. Nothing feeds the counters until input exists (milestone 3c).
		std::uint32_t input_state(call& c, bool& result)
		{
			value a;
			next_arg(c, a);
			std::int32_t held = a.word() ? c.vm.memory().read<std::int32_t>(a.word() + 0x0C) : 0;
			switch(c.index)
			{
			case 44: result = a.word() && held > 0; break;
			case 45: result = a.word() && held == 1; break;
			case 46: result = !a.word() || held < 0; break;
			case 47: result = a.word() && held == -1; break;
			}
			return c.args;
		}

		// 48 (0x48A840): at least `duration` ms passed since `timestamp` on the game's clock.
		std::uint32_t time_elapsed(call& c, bool& result)
		{
			value since, duration;
			next_arg(c, since);
			next_arg(c, duration);
			std::uint32_t now = c.vm.clock(), then = std::uint32_t(since.as_int());
			std::uint32_t passed = now >= then ? now - then : then - now;
			result = passed >= std::uint32_t(duration.as_int());
			return c.args;
		}

		// 49..53 (0x48A8A0): 49 an actor that exists and isn't switched off (standard game +0x29
		// bit 0 clear); 50..52 not null; 53 not -1.
		std::uint32_t valid(call& c, bool& result)
		{
			value a;
			next_arg(c, a);
			live_memory& m = c.vm.memory();
			switch(c.index)
			{
			case 49:
			{
				std::uint32_t actor = actor_value(c.vm, a);
				std::uint32_t standard = actor && m.contains(actor, 8) ? m.u32(actor + actor_field::standard_game) : 0;
				result = standard && !(m.u8(standard + standard_game_field::flags) & 1);
				break;
			}
			case 53: result = a.word() != 0xFFFFFFFF; break;
			default: result = a.word() != 0; break;
			}
			return c.args;
		}

		// 66 (0x48A950): a one-shot event flag in the actor's collision structure, cleared when read.
		std::uint32_t collision_event_flag(call& c, bool& result)
		{
			live_memory& m = c.vm.memory();
			std::uint32_t collision = actor_part(c.vm, c.self, actor_field::collision);
			result = false;
			if(collision && m.contains(collision + collision_event))
			{
				result = m.u8(collision + collision_event) != 0;
				m.write<std::uint8_t>(collision + collision_event, 0);
			}
			return c.args;
		}

		// 67, 68 (0x48A950): an actor's current reflex (67) or normal behaviour (68) is this one.
		std::uint32_t in_behaviour(call& c, bool& result)
		{
			value who, behaviour;
			next_arg(c, who);
			next_arg(c, behaviour);
			std::uint32_t mind = mind_of(c.vm, c.vm.super_object_of(actor_value(c.vm, who)));
			std::uint32_t intelligence = mind ? c.vm.memory().u32(mind + (c.index == 67 ? mind_field::reflex : mind_field::normal)) : 0;
			result = intelligence && c.vm.memory().u32(intelligence + intelligence_behaviour) == behaviour.word();
			return c.args;
		}

		// 70 (0x48B620): the actor plays this state.
		std::uint32_t in_state(call& c, bool& result)
		{
			value state;
			std::uint32_t self = c.vm.target(c.self, true);
			next_arg(c, state);
			std::uint32_t data = actor_part(c.vm, self, actor_field::data_3d);
			result = data && c.vm.memory().u32(data + data_3d_field::state) == state.word();
			return c.args;
		}

		// 75 (0x48B0C0): custom bit n (1-based) of the actor's standard game is set.
		std::uint32_t custom_bit(call& c, bool& result)
		{
			value n;
			std::uint32_t self = c.vm.target(c.self, true);
			next_arg(c, n);
			std::uint32_t standard = actor_part(c.vm, self, actor_field::standard_game);
			std::int32_t bit = n.as_int() - 1;
			result = standard && bit >= 0 && bit < 32 && (c.vm.memory().u32(standard + standard_game_field::custom_bits) >> bit & 1);
			return c.args;
		}

		// 119, 120 (0x48BA50): the actor is of this family (119) or has this AI model (120).
		std::uint32_t is_kind(call& c, bool& result)
		{
			value kind;
			std::uint32_t self = c.vm.target(c.self, true);
			next_arg(c, kind);
			live_memory& m = c.vm.memory();
			if(c.index == 119)
			{
				std::uint32_t data = actor_part(c.vm, self, actor_field::data_3d);
				result = data && m.u32(data + data_3d_field::family) == kind.word();
			}
			else
			{
				std::uint32_t mind = mind_of(c.vm, self);
				result = mind && m.u32(mind + mind_field::model) == kind.word();
			}
			return c.args;
		}

		// --- Functions ---------------------------------------------------------------------------

		// 0 (0x47B7D0): the actor's position.
		std::uint32_t position(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			std::uint32_t t = c.vm.actor_of(self) ? m.u32(self + super_object_transform) : 0;
			out = t ? value::vector(m.f32(t + 4), m.f32(t + 8), m.f32(t + 12)) : value::vector(0, 0, 0);
			return c.args;
		}

		// 43..48 (0x47C940): hit points (standard game +0x21), their current maximum (+0x22) and
		// the highest that maximum can go (+0x23). 44/45 add/remove hit points, 47/48 raise/lower
		// the maximum; each returns the new value.
		std::uint32_t hit_points(call& c, value& out)
		{
			value who;
			next_arg(c, who);
			std::int32_t amount = 0;
			if(c.index == 44 || c.index == 45 || c.index == 47 || c.index == 48)
			{
				value v;
				next_arg(c, v);
				amount = v.as_int();
			}
			live_memory& m = c.vm.memory();
			std::uint32_t actor = actor_value(c.vm, who);
			std::uint32_t standard = actor && m.contains(actor, 8) ? m.u32(actor + actor_field::standard_game) : 0;
			if(!standard)
			{
				out = value::integer(0);
				return c.args;
			}
			std::int32_t hp = m.u8(standard + standard_game_field::hit_points);
			std::int32_t max = m.u8(standard + standard_game_field::max_hit_points);
			std::int32_t top = m.u8(standard + standard_game_field::top_hit_points);
			switch(c.index)
			{
			case 43: out = value::integer(hp); return c.args;
			case 44: hp = std::min(hp + amount, max); break;
			case 45: hp = std::max(hp - amount, 0); break;
			case 46: out = value::integer(max); return c.args;
			case 47: max = std::min(max + amount, top); break;
			case 48: max = std::max(max - amount, 0); hp = std::min(hp, max); break;
			}
			m.write<std::uint8_t>(standard + standard_game_field::hit_points, std::uint8_t(hp));
			m.write<std::uint8_t>(standard + standard_game_field::max_hit_points, std::uint8_t(max));
			out = value::integer(c.index >= 47 ? max : hp);
			return c.args;
		}

		// 163 (0x478A20): minus the actor's Y axis (from its transform's rotation).
		std::uint32_t minus_y_axis(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			std::uint32_t t = c.vm.actor_of(self) ? m.u32(self + super_object_transform) : 0;
			out = t ? value::vector(-m.f32(t + 0x1C), -m.f32(t + 0x20), -m.f32(t + 0x24)) : value::vector(0, 0, 0);
			return c.args;
		}

		// 248, 249 (0x47A8E0): a variable that is an array of 32-bit words used as bit flags. 248
		// tests bit n (1-based), 249 counts the set bits from `from` to `to`.
		std::uint32_t bit_array(call& c, value& out)
		{
			value array;
			next_arg(c, array);
			live_memory& m = c.vm.memory();
			// The array: u32 element type, u8 count, elements from +8.
			auto bit = [&](std::int32_t n) {
				if(n < 1 || array.type != value_type::buffer)
					return false;
				std::uint32_t word = std::uint32_t(n - 1) / 32, b = std::uint32_t(n - 1) % 32;
				if(word >= m.u8(array.word() + 4))
					return false;
				return (m.u32(array.word() + 8 + word * 4) >> b & 1) != 0;
			};
			value from;
			next_arg(c, from);
			if(c.index == 248)
			{
				out = value::integer(bit(from.as_int()) ? 1 : 0);
				return c.args;
			}
			value to;
			next_arg(c, to);
			std::int32_t count = 0;
			for(std::int32_t n = from.as_int(); n <= to.as_int(); n++)
				count += bit(n);
			out = value::integer(count);
			return c.args;
		}

		// 31 (0x47BD70): absolute value, as a float.
		std::uint32_t absolute(call& c, value& out)
		{
			value a;
			next_arg(c, a);
			out = value::real(std::abs(a.as_float()));
			return c.args;
		}

		// 42 (0x47C810): move `current` towards `target`, keeping `factor` of the difference per
		// frame of the game's 60 per second (the exe raises the factor to the number of such frames
		// the last frame took; we always tick at 60, so that's once).
		std::uint32_t smooth(call& c, value& out)
		{
			value current, factor, target;
			next_arg(c, current);
			next_arg(c, factor);
			next_arg(c, target);
			float t = target.as_float();
			out = value::real(t + (current.as_float() - t) * factor.as_float());
			return c.args;
		}

		// 62 (0x478580): how long the last frame took, in ms (as a float).
		std::uint32_t frame_time(call& c, value& out)
		{
			out = value::real(float(c.vm.frame_ms()));
			return c.args;
		}

		// 150, 151 (0x478000): cross product, and normalising (vectors near zero stay as they are).
		std::uint32_t vector_math(call& c, value& out)
		{
			value a;
			next_arg(c, a);
			float x = a.component(0), y = a.component(1), z = a.component(2);
			if(c.index == 150)
			{
				value b;
				next_arg(c, b);
				float u = b.component(0), v = b.component(1), w = b.component(2);
				out = value::vector(y * w - z * v, z * u - x * w, x * v - y * u);
				return c.args;
			}
			constexpr float epsilon = 1e-9f;
			if(std::abs(x) < epsilon && std::abs(y) < epsilon && std::abs(z) < epsilon)
			{
				out = a;
				return c.args;
			}
			float length = std::sqrt(x * x + y * y + z * z);
			out = value::vector(x / length, y / length, z / length);
			return c.args;
		}

		// --- Procedures --------------------------------------------------------------------------

		// 174, 175 (0x4698A0): set or clear custom bit n (1-based), or the bits of a mask.
		std::uint32_t set_custom_bits(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value which, on;
			next_arg(c, which);
			next_arg(c, on);
			live_memory& m = c.vm.memory();
			std::uint32_t standard = actor_part(c.vm, self, actor_field::standard_game);
			if(!standard)
				return c.args;
			std::uint32_t bits = m.u32(standard + standard_game_field::custom_bits);
			if(c.index == 174)
			{
				std::int32_t n = which.as_int() - 1;
				if(n >= 0 && n < 32)
					bits = on.as_int() ? bits | 1u << n : bits & ~(1u << n);
			}
			else
			{
				std::uint32_t mask = std::uint32_t(which.as_int());
				bits = (mask & std::uint32_t(on.as_int())) | (~mask & bits);
			}
			m.write<std::uint32_t>(standard + standard_game_field::custom_bits, bits);
			return c.args;
		}

		// 69 (0x4696C0): switch the actor's normal behaviour. From the normal behaviour itself, its
		// rules start over under the new one this same frame.
		std::uint32_t change_behaviour(call& c)
		{
			value behaviour;
			std::uint32_t next = c.vm.eval(c.self, c.args, behaviour);
			std::uint32_t mind = mind_of(c.vm, c.self);
			bool from_reflex = c.vm.memory().u8(mind + mind_field::running_normal) == 0;
			c.vm.change_behaviour(c.vm.memory().u32(mind + mind_field::normal), behaviour.word(), from_reflex);
			return next;
		}
	}

	void register_handlers(interpreter& vm)
	{
		for(std::uint32_t i : { 0, 1, 2, 3 })
			vm.on_condition(i, logic);
		for(std::uint32_t i : { 4, 5, 6, 7, 8, 9 })
			vm.on_condition(i, compare);
		for(std::uint32_t i : { 44, 45, 46, 47 })
			vm.on_condition(i, input_state);
		vm.on_condition(48, time_elapsed);
		for(std::uint32_t i : { 49, 50, 51, 52, 53 })
			vm.on_condition(i, valid);
		vm.on_condition(66, collision_event_flag);
		vm.on_condition(67, in_behaviour);
		vm.on_condition(68, in_behaviour);
		vm.on_condition(70, in_state);
		vm.on_condition(75, custom_bit);
		vm.on_condition(119, is_kind);
		vm.on_condition(120, is_kind);

		vm.on_function(0, position);
		for(std::uint32_t i : { 43, 44, 45, 46, 47, 48 })
			vm.on_function(i, hit_points);
		vm.on_function(31, absolute);
		vm.on_function(42, smooth);
		vm.on_function(62, frame_time);
		vm.on_function(150, vector_math);
		vm.on_function(151, vector_math);
		vm.on_function(163, minus_y_axis);
		vm.on_function(248, bit_array);
		vm.on_function(249, bit_array);

		vm.on_procedure(69, change_behaviour);
		vm.on_procedure(174, set_custom_bits);
		vm.on_procedure(175, set_custom_bits);
	}
}
