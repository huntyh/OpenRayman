#include <openrayman/game/handlers.h>
#include <openrayman/game/animation_player.h>

#include <array>
#include <bit>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

// The conditions, functions and procedures scripts call, each written from what its handler in
// Rayman2.exe does (the address is given with each). Names are ours. See docs/formats/scripts.md.

namespace openrayman::game
{
	namespace
	{
		namespace actor_field
		{
			constexpr std::uint32_t data_3d = 0x00, standard_game = 0x04, dynamics = 0x08, brain = 0x0C, collision = 0x14;
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

		// The centre of an actor's bounding sphere in the world (super object +0x34: centre, radius,
		// in the actor's own space), or its position when it has none.
		std::array<float, 3> centre(interpreter& vm, std::uint32_t super_object)
		{
			live_memory& m = vm.memory();
			std::uint32_t t = vm.actor_of(super_object) ? m.u32(super_object + super_object_transform) : 0;
			if(!t)
				return {};
			std::array<float, 3> p{ m.f32(t + 4), m.f32(t + 8), m.f32(t + 12) };
			std::uint32_t sphere = m.u32(super_object + 0x34);
			if(sphere && m.contains(sphere, 16))
			{
				// The transform's matrix (+0x10) holds the actor's axes; the centre is in those.
				float c[3] = { m.f32(sphere), m.f32(sphere + 4), m.f32(sphere + 8) };
				for(int row = 0; row < 3; row++)
					for(int k = 0; k < 3; k++)
						p[std::size_t(k)] += c[row] * m.f32(t + 0x10 + std::uint32_t(row * 3 + k) * 4);
			}
			return p;
		}

		// 10 (0x47B480): the distance between two actors' bounding sphere centres.
		std::uint32_t distance(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value other;
			next_arg(c, other);
			std::uint32_t them = c.vm.super_object_of(actor_value(c.vm, other));
			if(!them)
			{
				// No such actor: far away (a missing actor must never seem close).
				out = value::real(1e9f);
				return c.args;
			}
			auto a = centre(c.vm, self), b = centre(c.vm, them);
			float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
			out = value::real(std::sqrt(dx * dx + dy * dy + dz * dz));
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

		// Calls whose systems don't exist yet: their arguments are evaluated (they may have side
		// effects) and nothing else happens. 115..119 (0x4691E0): messages to the engine's mailboxes
		// (not identified yet); 505: an on-screen text slot.
		std::uint32_t not_yet(call& c)
		{
			std::uint32_t at = c.args;
			std::uint8_t indent = c.vm.memory().u8(at - 8 + 6);
			while(c.vm.memory().u8(at + 6) > indent)
			{
				value ignored;
				at = c.vm.eval(c.self, at, ignored);
			}
			return at;
		}

		// 176, 182, 188, 194, 201, 207, 280 (0x46CBD0): set a vector of the actor's dynamics (the
		// physics, milestone 3c) and mark it as set in the dynamics flags (+8). Pairs differ in one
		// flag bit, meaning not known yet.
		std::uint32_t set_dynamics_vector(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			live_memory& m = c.vm.memory();
			std::uint32_t dynamics = actor_part(c.vm, self, actor_field::dynamics);
			std::uint32_t base = dynamics && m.contains(dynamics, 4) ? m.u32(dynamics) : 0;
			if(!base || !m.contains(base, 0x1A8))
				return c.args;
			struct target_field { std::uint32_t offset, set, bit; bool bit_on; };
			static const std::map<std::uint32_t, target_field> fields = {
				{ 176, { 0x24, 0xE0000, 0x4000, false } }, { 182, { 0x24, 0xE0000, 0x4000, true } },
				{ 188, { 0x30, 0x700000, 0x8000, false } }, { 194, { 0x30, 0x700000, 0x8000, true } },
				{ 201, { 0x190, 0x3800000, 0x10000, false } }, { 207, { 0x190, 0x3800000, 0x10000, true } },
				{ 280, { 0x19C, 0x1C000000, 0, false } },
			};
			const target_field& f = fields.at(c.index);
			for(int i = 0; i < 3; i++)
				m.write<float>(base + f.offset + std::uint32_t(i) * 4, v.component(i));
			std::uint32_t flags = m.u32(base + 8) | f.set;
			flags = f.bit_on ? flags | f.bit : flags & ~f.bit;
			m.write<std::uint32_t>(base + 8, flags);
			return c.args;
		}

		// 70 (0x469710): switch the reflex behaviour. 71 (0x469760): switch both, normal first.
		std::uint32_t change_behaviours(call& c)
		{
			live_memory& m = c.vm.memory();
			std::uint32_t mind = mind_of(c.vm, c.self);
			bool from_reflex = m.u8(mind + mind_field::running_normal) == 0;
			value first;
			next_arg(c, first);
			if(c.index == 70)
			{
				// From the normal behaviour, switching the reflex doesn't cut the running script short.
				if(std::uint32_t reflex = m.u32(mind + mind_field::reflex))
					c.vm.change_behaviour(reflex, first.word(), !from_reflex);
				return c.args;
			}
			value second;
			next_arg(c, second);
			if(std::uint32_t normal = m.u32(mind + mind_field::normal))
				c.vm.change_behaviour(normal, first.word(), from_reflex);
			if(std::uint32_t reflex = m.u32(mind + mind_field::reflex))
				c.vm.change_behaviour(reflex, second.word(), !from_reflex);
			return c.args;
		}

		// 72..75 (0x472290): change the actor's state (73 forced, 75 takes a number first). A refused
		// change stops the script run (flag 2).
		std::uint32_t change_state_procedure(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			if(c.index == 75)
			{
				value ignored;
				next_arg(c, ignored);
			}
			value target;
			next_arg(c, target);
			std::uint32_t data = actor_part(c.vm, self, actor_field::data_3d);
			if(data && !change_state(c.vm.memory(), data, target.word(), c.index == 73, c.vm.clock()))
				c.vm.signal(flow::wait);
			return c.args;
		}

		// 88 (0x467970): the flags the next statement's result merges with.
		std::uint32_t pending_signal(call& c)
		{
			value v;
			next_arg(c, v);
			c.vm.set_pending_signal(std::uint8_t(v.word()));
			return c.args;
		}

		// 139 (0x467850): take an actor out of the world. When it's the running actor, its scripts
		// stop for this frame (flag 3).
		std::uint32_t remove_actor(call& c)
		{
			value who;
			next_arg(c, who);
			std::uint32_t target = c.vm.super_object_of(actor_value(c.vm, who));
			if(!target)
				return c.args;
			c.vm.remove(target);
			if(target == c.self)
				c.vm.signal(3);
			return c.args;
		}

		// 312 (0x46D920): set or clear bit n (0-based) of the super object's flags (+0x30).
		std::uint32_t super_object_flag(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value bit, on;
			next_arg(c, bit);
			next_arg(c, on);
			live_memory& m = c.vm.memory();
			std::int32_t b = bit.as_int();
			if(!c.vm.actor_of(self) || b < 0 || b >= 32)
				return c.args;
			std::uint32_t flags = m.u32(self + 0x30);
			m.write<std::uint32_t>(self + 0x30, on.as_int() ? flags | 1u << b : flags & ~(1u << b));
			return c.args;
		}

		// 321..332 (0x46DFB0): a zone's 2-bit mode in the actor's collision set: four words (+0x30,
		// +0x38, +0x34, +0x3C by kind), 16 zones each. 321..324 set mode 1, 325..328 mode 2, 329..332
		// clear, for kinds 0..3.
		std::uint32_t zone_mode(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value n;
			next_arg(c, n);
			live_memory& m = c.vm.memory();
			std::uint32_t collision = actor_part(c.vm, self, actor_field::collision);
			std::int32_t zone = n.as_int() - 1;
			if(!collision || zone < 0 || zone >= 16)
				return c.args;
			static constexpr std::uint32_t words[] = { 0x30, 0x38, 0x34, 0x3C };
			std::uint32_t kind = (c.index - 321) % 4, mode = c.index < 325 ? 1 : c.index < 329 ? 2 : 0;
			std::uint32_t at = collision + words[kind];
			std::uint32_t word = m.u32(at) & ~(3u << (zone * 2));
			m.write<std::uint32_t>(at, word | mode << (zone * 2));
			return c.args;
		}

		// 341 (0x46E2A0): how often the brain runs (3D data +0xFE); the counter restarts if past it.
		std::uint32_t brain_period(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			live_memory& m = c.vm.memory();
			std::uint32_t data = actor_part(c.vm, self, actor_field::data_3d);
			if(!data)
				return c.args;
			std::uint8_t period = std::uint8_t(v.word());
			m.write<std::uint8_t>(data + 0xFE, period);
			if(std::int8_t(m.u8(data + 0xFF)) >= std::int8_t(period))
				m.write<std::uint8_t>(data + 0xFF, 0);
			return c.args;
		}

		// 378..385 (0x470770): a global gauge, a value and its maximum (0x4E9308, 0x4E930C; functions
		// 102 and 103 read them). What it measures isn't known yet.
		float g_gauge = 0, g_gauge_max = 0;
		std::uint32_t gauge(call& c)
		{
			value v;
			next_arg(c, v);
			float x = v.as_float();
			switch(c.index)
			{
			case 378: case 380: g_gauge = x; break;
			case 379: case 381: g_gauge_max = x; break;
			case 382: g_gauge = std::clamp(g_gauge + x, 0.0f, g_gauge_max); break;
			case 383: g_gauge_max = std::max(g_gauge_max + x, 0.0f); g_gauge = std::min(g_gauge, g_gauge_max); break;
			case 384: g_gauge = std::clamp(g_gauge - x, 0.0f, g_gauge_max); break;
			case 385: g_gauge_max = std::max(g_gauge_max - x, 0.0f); g_gauge = std::min(g_gauge, g_gauge_max); break;
			}
			return c.args;
		}

		// 396..398 (0x470C80): the actor's capability bits (standard game +0x1C): set, add, remove.
		std::uint32_t capabilities(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			live_memory& m = c.vm.memory();
			std::uint32_t standard = actor_part(c.vm, self, actor_field::standard_game);
			if(!standard)
				return c.args;
			std::uint32_t bits = m.u32(standard + 0x1C), x = std::uint32_t(v.as_int());
			bits = c.index == 396 ? x : c.index == 397 ? bits | x : bits & ~x;
			m.write<std::uint32_t>(standard + 0x1C, bits);
			return c.args;
		}

		// The actor's dynamics block (the physics, milestone 3c), or 0.
		std::uint32_t dynamics_base(call& c, std::uint32_t self, std::uint32_t size)
		{
			live_memory& m = c.vm.memory();
			std::uint32_t dynamics = actor_part(c.vm, self, actor_field::dynamics);
			std::uint32_t base = dynamics && m.contains(dynamics, 4) ? m.u32(dynamics) : 0;
			return base && m.contains(base, size) ? base : 0;
		}

		// 244..261 (0x46C1D0): one dynamics flag bit each.
		std::uint32_t dynamics_flag(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value on;
			next_arg(c, on);
			std::uint32_t base = dynamics_base(c, self, 0x10);
			static const std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> bits = {
				{ 244, { 8, 0x1 } }, { 245, { 8, 0x2 } }, { 246, { 8, 0x4 } }, { 247, { 8, 0x8 } }, { 248, { 8, 0x10 } },
				{ 249, { 8, 0x40 } }, { 250, { 8, 0x20 } }, { 251, { 0xC, 0x100 } }, { 252, { 0xC, 0x200 } },
				{ 253, { 0xC, 0x100000 } }, { 254, { 0xC, 0x200000 } }, { 256, { 8, 0x80 } }, { 257, { 8, 0x100 } },
				{ 258, { 8, 0x200 } }, { 259, { 8, 0x400 } }, { 260, { 8, 0x800 } }, { 261, { 8, 0x1000 } },
			};
			auto it = bits.find(c.index);
			if(!base || it == bits.end())
				return c.args;
			live_memory& m = c.vm.memory();
			auto [word, bit] = it->second;
			std::uint32_t flags = m.u32(base + word);
			m.write<std::uint32_t>(base + word, on.as_int() ? flags | bit : flags & ~bit);
			return c.args;
		}

		// 181 (0x46CE00): procedure 176's vector, from three numbers.
		std::uint32_t set_dynamics_xyz(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value x, y, z;
			next_arg(c, x);
			next_arg(c, y);
			next_arg(c, z);
			std::uint32_t base = dynamics_base(c, self, 0x30);
			if(!base)
				return c.args;
			live_memory& m = c.vm.memory();
			m.write<float>(base + 0x24, x.as_float());
			m.write<float>(base + 0x28, y.as_float());
			m.write<float>(base + 0x2C, z.as_float());
			m.write<std::uint32_t>(base + 8, (m.u32(base + 8) | 0xE0000) & ~0x4000u);
			return c.args;
		}

		// --- Transforms ----------------------------------------------------------------------------
		// A super object's global transform (+0x24): u32 type, position, rotation (3 rows: the
		// actor's X, Y, Z axes in the world; the game multiplies row vectors), then the same rows
		// times the scale. Its forward is -Y.

		using vec3 = std::array<float, 3>;

		float dot(const vec3& a, const vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
		vec3 sub(const vec3& a, const vec3& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
		vec3 cross(const vec3& a, const vec3& b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
		float length(const vec3& a) { return std::sqrt(dot(a, a)); }
		vec3 normalised(const vec3& a)
		{
			float l = length(a);
			return l > 1e-9f ? vec3{ a[0] / l, a[1] / l, a[2] / l } : a;
		}
		vec3 vec_of(const value& v) { return { v.component(0), v.component(1), v.component(2) }; }
		value value_of(const vec3& v) { return value::vector(v[0], v[1], v[2]); }

		std::uint32_t transform_of(interpreter& vm, std::uint32_t super_object)
		{
			return vm.actor_of(super_object) ? vm.memory().u32(super_object + super_object_transform) : 0;
		}
		vec3 read3(live_memory& m, std::uint32_t at) { return { m.f32(at), m.f32(at + 4), m.f32(at + 8) }; }
		void write3(live_memory& m, std::uint32_t at, const vec3& v)
		{
			for(int i = 0; i < 3; i++)
				m.write<float>(at + std::uint32_t(i) * 4, v[std::size_t(i)]);
		}
		vec3 position_of(interpreter& vm, std::uint32_t super_object)
		{
			std::uint32_t t = transform_of(vm, super_object);
			return t ? read3(vm.memory(), t + 4) : vec3{};
		}
		vec3 axis_of(interpreter& vm, std::uint32_t super_object, int axis)
		{
			std::uint32_t t = transform_of(vm, super_object);
			return t ? read3(vm.memory(), t + 0x10 + std::uint32_t(axis) * 12) : vec3{};
		}
		// A world vector in an actor's own axes (assumes they are perpendicular).
		vec3 to_local(interpreter& vm, std::uint32_t super_object, const vec3& v)
		{
			return { dot(v, axis_of(vm, super_object, 0)), dot(v, axis_of(vm, super_object, 1)), dot(v, axis_of(vm, super_object, 2)) };
		}

		// Writes new axes into a transform, keeping each axis' scale in the second copy (0x445D00).
		void set_axes(live_memory& m, std::uint32_t t, const vec3& x, const vec3& y, const vec3& z)
		{
			const vec3 axes[3] = { x, y, z };
			for(int i = 0; i < 3; i++)
			{
				float scale = length(read3(m, t + 0x34 + std::uint32_t(i) * 12));
				if(scale < 1e-6f)
					scale = 1;
				write3(m, t + 0x10 + std::uint32_t(i) * 12, axes[i]);
				const vec3& a = axes[i];
				write3(m, t + 0x34 + std::uint32_t(i) * 12, { a[0] * scale, a[1] * scale, a[2] * scale });
			}
			// Rotation and scale from here on (type 4: the second copy is what places the object).
			if(m.u32(t) < 4)
				m.write<std::uint32_t>(t, 4);
		}

		// 3 (0x47B6C0): distance between the actors' positions (far away when there's no other).
		std::uint32_t distance_to(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value other;
			next_arg(c, other);
			std::uint32_t them = c.vm.super_object_of(actor_value(c.vm, other));
			out = value::real(them ? length(sub(position_of(c.vm, self), position_of(c.vm, them))) : 3.4028234e38f);
			return c.args;
		}

		// 2, 4..9, 11..17 (0x47B8D0): where another actor is, seen from it: the offset from it to us
		// in its own axes, as one component, the length of two, or (2) the angle in degrees between
		// our forward and the direction to it, in the horizontal plane. 11..16 use the bounding
		// sphere centres. 17: distance to a waypoint.
		std::uint32_t relative(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value other;
			next_arg(c, other);
			live_memory& m = c.vm.memory();
			if(c.index == 17)
			{
				std::uint32_t w = other.word();
				out = value::real(w && m.contains(w, 16) ? length(sub(position_of(c.vm, self), read3(m, w))) : 3.4028234e38f);
				return c.args;
			}
			std::uint32_t them = c.vm.super_object_of(actor_value(c.vm, other));
			if(!them)
			{
				out = value::real(3.4028234e38f);
				return c.args;
			}
			bool centres = c.index >= 11;
			vec3 v = centres ? sub(centre(c.vm, self), centre(c.vm, them)) : sub(position_of(c.vm, self), position_of(c.vm, them));
			if(c.index == 2)
			{
				vec3 forward = axis_of(c.vm, self, 1);
				float heading = std::atan2(-forward[1], -forward[0]);
				float towards = std::atan2(-v[1], -v[0]);
				float d = towards - heading;
				constexpr float pi = 3.14159265f;
				while(d > pi)
					d -= 2 * pi;
				while(d < -pi)
					d += 2 * pi;
				out = value::real(d * 57.29578f);
				return c.args;
			}
			vec3 l = to_local(c.vm, them, v);
			switch(centres ? c.index - 7 : c.index)
			{
			case 4: out = value::real(l[0]); break;
			case 5: out = value::real(l[1]); break;
			case 6: out = value::real(l[2]); break;
			case 7: out = value::real(std::sqrt(l[0] * l[0] + l[1] * l[1])); break;
			case 8: out = value::real(std::sqrt(l[0] * l[0] + l[2] * l[2])); break;
			case 9: out = value::real(std::sqrt(l[1] * l[1] + l[2] * l[2])); break;
			}
			return c.args;
		}

		// 21..30, 32..35 (0x47BDC0): maths on floats.
		std::uint32_t float_math(call& c, value& out)
		{
			value a;
			next_arg(c, a);
			float x = a.as_float();
			auto more = [&] {
				value v;
				next_arg(c, v);
				return v.as_float();
			};
			switch(c.index)
			{
			case 21: out = value::real(x); break;
			case 22: out = value::real(std::sin(x * 0.0174533f)); break;
			case 23: out = value::real(std::cos(x * 0.0174533f)); break;
			case 24: out = value::real(x * x); break;
			case 25: out = value::real(std::sqrt(std::max(x, 0.0f))); break;
			case 26:
			{
				// The game reads a table of pre-made random numbers; any uniform source does.
				float y = more();
				static std::uint32_t seed = 12345;
				seed = seed * 1664525u + 1013904223u;
				out = value::real(x + (y - x) * float(seed >> 8) / float(1 << 24));
				break;
			}
			case 27: out = value::real(std::min(x, more())); break;
			case 28: out = value::real(std::max(x, more())); break;
			case 29: out = value::real(x * 0.0174533f); break;
			case 30: out = value::real(x * 57.29578f); break;
			case 32:
			{
				float lo = more(), hi = more();
				out = value::real(std::clamp(x, lo, std::max(lo, hi)));
				break;
			}
			case 33: out = value::real(x > 0 ? 1.0f : x < 0 ? -1.0f : 0.0f); break;
			case 34: out = value::real(x * x * x); break;
			case 35:
			{
				float lo = more(), hi = more(), span = hi - lo;
				if(span > 0)
				{
					while(x >= hi)
						x -= span;
					while(x < lo)
						x += span;
				}
				out = value::real(x);
				break;
			}
			}
			return c.args;
		}

		// 52, 54 (0x47C250): a vector against the actor's axes (x = X axis . v, ...).
		std::uint32_t against_axes(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			out = value_of(to_local(c.vm, self, vec_of(v)));
			return c.args;
		}

		// 60, 61 (0x47CAF0): the game clock, and how far it is from a time (no sign).
		std::uint32_t clock_function(call& c, value& out)
		{
			std::uint32_t now = c.vm.clock();
			if(c.index == 60)
			{
				out = value::integer(std::int32_t(now));
				return c.args;
			}
			value then;
			next_arg(c, then);
			std::uint32_t t = std::uint32_t(then.as_int());
			out = value::integer(std::int32_t(now >= t ? now - t : t - now));
			return c.args;
		}

		// 149 (0x4784F0): dot product.
		std::uint32_t dot_product(call& c, value& out)
		{
			value a, b;
			next_arg(c, a);
			next_arg(c, b);
			out = value::real(dot(vec_of(a), vec_of(b)));
			return c.args;
		}

		// 162, 164, 165 (0x478AA0): the actor's scale, X axis, Z axis (up: (0,0,1) when dynamics
		// flag 0x08 is set, not handled yet).
		std::uint32_t matrix_vector(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			std::uint32_t t = transform_of(c.vm, self);
			if(!t)
			{
				out = value::vector(0, 0, 0);
				return c.args;
			}
			switch(c.index)
			{
			case 162:
				out = value::vector(length(read3(m, t + 0x34)), length(read3(m, t + 0x40)), length(read3(m, t + 0x4C)));
				break;
			case 164: out = value_of(read3(m, t + 0x10)); break;
			case 165: out = value_of(read3(m, t + 0x28)); break;
			}
			return c.args;
		}

		// 180..185 (0x47C350): vector maths: project, mix, mix (frame-rate scaled, the same at our
		// fixed 60 per second), scale, length, rotate about an axis.
		std::uint32_t vector_ops(call& c, value& out)
		{
			value a;
			next_arg(c, a);
			vec3 va = vec_of(a);
			switch(c.index)
			{
			case 180:
			{
				value b;
				next_arg(c, b);
				vec3 vb = vec_of(b);
				if(length(vb) < 1e-9f)
				{
					out = a;
					break;
				}
				vec3 n = normalised(vb);
				float d = dot(va, n);
				out = value::vector(n[0] * d, n[1] * d, n[2] * d);
				break;
			}
			case 181: case 182:
			{
				value t, b;
				next_arg(c, t);
				next_arg(c, b);
				vec3 vc = vec_of(b);
				float f = t.as_float();
				out = value::vector(vc[0] + (va[0] - vc[0]) * f, vc[1] + (va[1] - vc[1]) * f, vc[2] + (va[2] - vc[2]) * f);
				break;
			}
			case 183:
			{
				value s;
				next_arg(c, s);
				float f = s.as_float();
				out = value::vector(va[0] * f, va[1] * f, va[2] * f);
				break;
			}
			case 184: out = value::real(length(va)); break;
			case 185:
			{
				value degrees;
				next_arg(c, degrees);
				if(degrees.as_float() == 0)
				{
					c.args = c.vm.skip(c.args);
					out = a;
					break;
				}
				value axis;
				next_arg(c, axis);
				// Rodrigues' rotation by -angle (the exe builds the quaternion from -angle/2).
				vec3 k = normalised(vec_of(axis));
				float angle = -degrees.as_float() * 0.0174533f, cs = std::cos(angle), sn = std::sin(angle);
				vec3 kxv = cross(k, va);
				float kv = dot(k, va);
				out = value::vector(va[0] * cs + kxv[0] * sn + k[0] * kv * (1 - cs), va[1] * cs + kxv[1] * sn + k[1] * kv * (1 - cs),
					va[2] * cs + kxv[2] * sn + k[2] * kv * (1 - cs));
				break;
			}
			}
			return c.args;
		}

		// 240 (0x47A3E0): the nearest actor above this one in the scene (what it is attached to).
		std::uint32_t parent_actor(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			out = value::of(value_type::actor, 0);
			for(std::uint32_t so = self ? m.u32(self + 0x1C) : 0; so && m.contains(so, 8); so = m.u32(so + 0x1C))
			{
				if(m.u32(so) == 2)
				{
					out = value::of(value_type::actor, m.u32(so + 4));
					break;
				}
			}
			return c.args;
		}

		// --- More conditions ---------------------------------------------------------------------

		// 27..32 (0x48A330): the contact flags of the actor's dynamics report (dynamics base +0x150,
		// flags at +4), filled by collisions (milestone 3c).
		std::uint32_t contact(call& c, bool& result)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			std::uint32_t base = dynamics_base(c, self, 0x154);
			std::uint32_t report = base ? m.u32(base + 0x150) : 0;
			result = false;
			if(!report || !m.contains(report + 4, 4))
				return c.args;
			std::uint32_t flags = m.u32(report + 4);
			switch(c.index)
			{
			case 27: result = flags & 0x01; break;
			case 28: result = flags & 0x04; break;
			case 29: result = flags == 0; break;
			case 30: result = flags & 0x10; break;
			case 31: result = flags & 0x10000; break;
			case 32: result = flags & 0x40; break;
			}
			return c.args;
		}

		// 65 (0x48A950): the two numbers share a bit. 72: the game mode (0x500380) isn't 9.
		std::uint32_t bits_overlap(call& c, bool& result)
		{
			if(c.index == 72)
			{
				result = true;
				return c.args;
			}
			value a, b;
			next_arg(c, a);
			next_arg(c, b);
			result = (std::uint32_t(a.as_int()) & std::uint32_t(b.as_int())) != 0;
			return c.args;
		}

		// 74 (0x48B190): an actor stands inside a waypoint's sphere (position, radius at +0xC).
		std::uint32_t inside_waypoint(call& c, bool& result)
		{
			value who, waypoint;
			next_arg(c, who);
			next_arg(c, waypoint);
			live_memory& m = c.vm.memory();
			std::uint32_t so = c.vm.super_object_of(actor_value(c.vm, who));
			std::uint32_t w = waypoint.word();
			result = false;
			if(!so || !w || !m.contains(w, 16))
				return c.args;
			vec3 d = sub(read3(m, w), position_of(c.vm, so));
			float r = m.f32(w + 0xC);
			result = dot(d, d) <= r * r;
			return c.args;
		}

		// 78 (0x48B0C0): the current state's flags (+0x2B) share a bit with the mask.
		std::uint32_t state_flag(call& c, bool& result)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value mask;
			next_arg(c, mask);
			live_memory& m = c.vm.memory();
			std::uint32_t data = actor_part(c.vm, self, actor_field::data_3d);
			std::uint32_t s = data ? m.u32(data + 4) : 0;
			result = s && (m.u8(s + 0x2B) & std::uint32_t(mask.as_int())) != 0;
			return c.args;
		}

		// 103 (0x48B2D0): the vector is (nearly) zero.
		std::uint32_t is_zero_vector(call& c, bool& result)
		{
			value v;
			next_arg(c, v);
			vec3 a = vec_of(v);
			result = std::abs(a[0]) < 1e-5f && std::abs(a[1]) < 1e-5f && std::abs(a[2]) < 1e-5f;
			return c.args;
		}

		// --- Transform procedures ----------------------------------------------------------------

		// 200 (0x46BF20): move an actor (the first argument, not the usual target) to a position.
		std::uint32_t teleport(call& c)
		{
			value who, where;
			next_arg(c, who);
			next_arg(c, where);
			std::uint32_t so = c.vm.super_object_of(actor_value(c.vm, who));
			if(std::uint32_t t = transform_of(c.vm, so))
				write3(c.vm.memory(), t + 4, vec_of(where));
			return c.args;
		}

		// 503 (0x463390): start an effect: kind, a point, a vector, a game material, a number. Kinds
		// 23 and 28 so far (game/src/particles.cc); the others do nothing yet.
		std::uint32_t start_effect(call& c)
		{
			value kind, point, vector, material, number;
			next_arg(c, kind);
			next_arg(c, point);
			next_arg(c, vector);
			next_arg(c, material);
			next_arg(c, number);
			c.vm.particles().start(kind.as_int(), vec_of(point), vec_of(vector), material.word(), number.as_float(), c.vm.clock() / 1000.0);
			return c.args;
		}

		// 433 (0x46A890): turn to face a horizontal direction (up stays (0,0,1); forward is -Y).
		std::uint32_t face_horizontal(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			std::uint32_t t = transform_of(c.vm, self);
			if(!t)
				return c.args;
			vec3 d = vec_of(v);
			d[2] = 0;
			if(length(d) < 1e-9f)
				return c.args;
			vec3 forward = normalised(d), up{ 0, 0, 1 };
			vec3 y{ -forward[0], -forward[1], 0 };
			vec3 x = cross(y, up);
			set_axes(c.vm.memory(), t, x, y, up);
			return c.args;
		}

		// 161 (0x46AB20): look along a direction (forward, -Y, along it), keeping up as close as
		// possible to the old Z axis.
		std::uint32_t face_direction(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			std::uint32_t t = transform_of(c.vm, self);
			vec3 d = vec_of(v);
			if(!t || length(d) < 1e-9f)
				return c.args;
			live_memory& m = c.vm.memory();
			vec3 y = normalised({ -d[0], -d[1], -d[2] });
			vec3 z = read3(m, t + 0x28);
			vec3 x = cross(y, z);
			if(length(x) < 1e-6f)
				x = read3(m, t + 0x10);
			x = normalised(x);
			z = normalised(cross(x, y));
			set_axes(m, t, x, y, z);
			return c.args;
		}

		// 171 (0x469CB0): turn so two given directions become two of the actor's axes. The mode
		// picks them (tables 0x46A848, 0x46A860): A becomes -Y, -Y, Z, Z, X, X and B becomes Z, X,
		// -Y, X, Z, -Y for modes 0..5. A zero vector keeps the current axis. A stays exact, the
		// third axis is the cross product of the other two (right-handed), then B is made square.
		std::uint32_t orient_from_vectors(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value va, vb, vmode;
			next_arg(c, va);
			next_arg(c, vb);
			next_arg(c, vmode);
			std::uint32_t t = transform_of(c.vm, self);
			std::uint32_t mode = std::uint32_t(vmode.as_int());
			if(!t || mode > 5)
				return c.args;
			live_memory& m = c.vm.memory();
			static const int axis_a[6] = { 1, 1, 2, 2, 0, 0 }, axis_b[6] = { 2, 0, 1, 0, 2, 1 };
			int a = axis_a[mode], b = axis_b[mode], third = 3 - a - b;
			vec3 axes[3] = { read3(m, t + 0x10), read3(m, t + 0x1C), read3(m, t + 0x28) };
			auto take = [&](const value& v, int axis, bool flip) {
				vec3 d = vec_of(v);
				if(length(d) < 1e-9f)
					return;
				d = normalised(d);
				axes[axis] = flip ? vec3{ -d[0], -d[1], -d[2] } : d;
			};
			take(va, a, a == 1);
			take(vb, b, b == 1);
			auto rebuild = [&](int axis) {
				vec3 v = normalised(cross(axes[(axis + 1) % 3], axes[(axis + 2) % 3]));
				if(length(v) > 0.5f)
					axes[axis] = v;
			};
			rebuild(third);
			rebuild(b);
			set_axes(m, t, axes[0], axes[1], axes[2]);
			return c.args;
		}

		// 163..165 (0x46AB20): turn about the actor's own X, Y or Z axis by degrees.
		std::uint32_t rotate_local(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value degrees;
			next_arg(c, degrees);
			std::uint32_t t = transform_of(c.vm, self);
			if(!t)
				return c.args;
			live_memory& m = c.vm.memory();
			vec3 axes[3] = { read3(m, t + 0x10), read3(m, t + 0x1C), read3(m, t + 0x28) };
			int about = int(c.index - 163);
			int i = (about + 1) % 3, j = (about + 2) % 3;
			float a = degrees.as_float() * 0.0174533f, cs = std::cos(a), sn = std::sin(a);
			vec3 u = axes[i], w = axes[j];
			for(int k = 0; k < 3; k++)
			{
				axes[i][std::size_t(k)] = u[std::size_t(k)] * cs + w[std::size_t(k)] * sn;
				axes[j][std::size_t(k)] = w[std::size_t(k)] * cs - u[std::size_t(k)] * sn;
			}
			set_axes(m, t, axes[0], axes[1], axes[2]);
			return c.args;
		}

		// 173 (0x46AB20): set the scale along the actor's axes.
		std::uint32_t set_scale(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			std::uint32_t t = transform_of(c.vm, self);
			live_memory& m = c.vm.memory();
			if(!t || (m.u32(self + 0x30) & 0x10))
				return c.args;
			vec3 s = vec_of(v);
			if(m.u32(self + 0x30) & 0x100)
				s = { s[0], s[0], s[0] };
			for(int i = 0; i < 3; i++)
			{
				vec3 a = read3(m, t + 0x10 + std::uint32_t(i) * 12);
				float f = s[std::size_t(i)];
				write3(m, t + 0x34 + std::uint32_t(i) * 12, { a[0] * f, a[1] * f, a[2] * f });
			}
			m.write<std::uint32_t>(t, 4);
			return c.args;
		}

		// 235, 236 (0x46DCE0): forget what the actor stands on; clear its contact flags.
		std::uint32_t clear_contacts(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			std::uint32_t base = dynamics_base(c, self, 0x154);
			std::uint32_t report = base ? m.u32(base + 0x150) : 0;
			if(!report || !m.contains(report, 0xA8))
				return c.args;
			if(c.index == 235)
			{
				m.write<std::uint32_t>(report + 0xA4, 0);
				m.write<std::uint32_t>(report + 4, m.u32(report + 4) & ~0x10000u);
			}
			else
				m.write<std::uint32_t>(report + 4, 0);
			return c.args;
		}

		// --- Meta-actions (0x4A6418): start once, then checked every frame. The check returns
		// flow::stop while running (the scripts hold here) and flow::done when finished. Scratch is 8
		// slots of 12 bytes.

		meta_handler wait_time()
		{
			// 0: wait a number of ms on the game clock (slot 0: the deadline).
			return { [](call& c, std::uint32_t scratch) {
						value ms;
						next_arg(c, ms);
						c.vm.memory().write<std::uint32_t>(scratch, c.vm.clock() + std::uint32_t(ms.as_int()));
						return c.args;
					},
				[](interpreter& vm, std::uint32_t, std::uint32_t scratch) -> std::uint8_t {
					return vm.clock() < vm.memory().u32(scratch) ? flow::stop : flow::done;
				} };
		}

		// The current animation has played to its end and nothing else is queued (0x410310).
		bool animation_finished(interpreter& vm, std::uint32_t self)
		{
			live_memory& m = vm.memory();
			std::uint32_t data = actor_part(vm, self, actor_field::data_3d);
			return data && m.u8(data + data_3d::finished) && m.u32(data + data_3d::current_state) == m.u32(data + data_3d::next_state);
		}

		// 198 (0x46CE00, case 0x46D2BC): the X and Y of the dynamics vector at +0x30, stored as given,
		// with flags 0x308000. 288 (case 0x46D758): forget procedure 280's vector (+0x19C): clear
		// flags 0x1C000000.
		std::uint32_t set_dynamics_xy(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value x, y;
			if(c.index == 198)
			{
				next_arg(c, x);
				next_arg(c, y);
			}
			std::uint32_t base = dynamics_base(c, self, 0x38);
			if(!base)
				return c.args;
			live_memory& m = c.vm.memory();
			if(c.index == 198)
			{
				m.write<std::uint32_t>(base + 0x30, x.word());
				m.write<std::uint32_t>(base + 0x34, y.word());
				m.write<std::uint32_t>(base + 8, m.u32(base + 8) | 0x308000);
			}
			else
				m.write<std::uint32_t>(base + 8, m.u32(base + 8) & ~0x1C000000u);
			return c.args;
		}

		// 77 (0x48B620): the current animation has played to its end.
		std::uint32_t animation_done(call& c, bool& result)
		{
			result = animation_finished(c.vm, c.vm.target(c.self, true));
			return c.args;
		}

		// 79..102 (0x48B380): one bit of the dynamics flag words (+8, +0xC), the same bits procedures
		// 244..261 set. 79/80 and 100 clear the bit they test. 101 also hands the vector at dynamics
		// +0x1FC to the variable given as its argument when the bit is set.
		std::uint32_t dynamics_bit(call& c, bool& result)
		{
			static const std::pair<std::uint32_t, std::uint32_t> bits[24] = {
				{ 0xC, 0x10000 }, { 0xC, 0x10000 }, { 8, 0x1 }, { 8, 0x2 }, { 8, 0x4 }, { 8, 0x8 }, { 8, 0x10 }, { 8, 0x40 },
				{ 8, 0x20 }, { 0xC, 0x100 }, { 0xC, 0x200 }, { 0xC, 0x200 }, { 0xC, 0x200000 }, { 8, 0x80 }, { 8, 0x100 },
				{ 8, 0x200 }, { 8, 0x400 }, { 8, 0x800 }, { 8, 0x1000 }, { 0xC, 0x20 }, { 8, 0x2000 }, { 0xC, 0x2000 },
				{ 0xC, 0x40 }, { 0xC, 0x80000 },
			};
			std::uint32_t self = c.vm.target(c.self, true);
			std::uint32_t base = dynamics_base(c, self, 0x208);
			result = false;
			if(!base)
				return c.index == 101 ? c.vm.skip(c.args) : c.args;
			live_memory& m = c.vm.memory();
			auto [word, bit] = bits[c.index - 79];
			std::uint32_t flags = m.u32(base + word);
			result = (flags & bit) != 0;
			if(c.index == 79 || c.index == 80 || c.index == 100)
				m.write<std::uint32_t>(base + word, flags & ~bit);
			if(c.index != 101)
				return c.args;
			if(!result)
				return c.vm.skip(c.args);
			return c.vm.assign(c.self, c.args, value::vector(m.f32(base + 0x1FC), m.f32(base + 0x200), m.f32(base + 0x204)));
		}

		// 102, 103 (0x468B10 -> 0x4195A0): hide (102) or show (103) one part of the actor, or all of
		// them for 0xFFFF. Parts: 3D data +0x90, count +0x94, 0x14 bytes each: super object at +4 (+0
		// when that is null), the flag kept at +0x11; hiding clears bits 0, 1 and 7 of the part's
		// super object flags (+0x30), showing sets them.
		std::uint32_t show_parts(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value which;
			next_arg(c, which);
			live_memory& m = c.vm.memory();
			std::uint32_t data = actor_part(c.vm, self, actor_field::data_3d);
			std::uint32_t parts = data && m.contains(data + 0x90, 8) ? m.u32(data + 0x90) : 0;
			if(!parts)
				return c.args;
			std::uint32_t count = m.u32(data + 0x94);
			std::uint8_t hide = c.index == 102;
			std::uint16_t index = std::uint16_t(which.as_int());
			auto apply = [&](std::uint32_t entry) {
				if(!m.contains(entry, 0x14))
					return;
				m.write<std::uint8_t>(entry + 0x11, hide);
				std::uint32_t part = m.u32(entry + 4) ? m.u32(entry + 4) : m.u32(entry);
				if(part && m.contains(part + 0x30, 4))
				{
					std::uint32_t flags = m.u32(part + 0x30);
					m.write<std::uint32_t>(part + 0x30, hide ? flags & ~0x83u : flags | 0x83u);
				}
			};
			if(index != 0xFFFF)
				apply(parts + index * 0x14u);
			else
				for(std::uint32_t i = 0; i < count; i++)
					apply(parts + i * 0x14);
			return c.args;
		}

		meta_handler change_state_and_wait()
		{
			// 1: change state, then wait until it plays (slot 0: success, slot 1: the state).
			return { [](call& c, std::uint32_t scratch) {
						value s;
						next_arg(c, s);
						live_memory& m = c.vm.memory();
						std::uint32_t data = actor_part(c.vm, c.self, actor_field::data_3d);
						bool ok = false;
						if(data)
						{
							m.write<std::uint32_t>(data + data_3d::requested_state, 0);
							ok = change_state(m, data, s.word(), false, c.vm.clock());
						}
						m.write<std::uint32_t>(scratch, ok ? 1 : 0);
						m.write<std::uint32_t>(scratch + 12, s.word());
						return c.args;
					},
				[](interpreter& vm, std::uint32_t self, std::uint32_t scratch) -> std::uint8_t {
					live_memory& m = vm.memory();
					std::uint32_t data = actor_part(vm, self, actor_field::data_3d);
					if(!m.u32(scratch) || !data || m.u32(data + data_3d::requested_state) != m.u32(scratch + 12))
						return flow::done;
					return animation_finished(vm, self) ? flow::done : flow::stop;
				} };
		}

		meta_handler wait_animation()
		{
			// 2: wait for the current animation to end.
			return { [](call& c, std::uint32_t) { return c.args; },
				[](interpreter& vm, std::uint32_t self, std::uint32_t) -> std::uint8_t {
					return animation_finished(vm, self) ? flow::done : flow::stop;
				} };
		}

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
		for(std::uint32_t i = 27; i <= 32; i++)
			vm.on_condition(i, contact);
		vm.on_condition(77, animation_done);
		for(std::uint32_t i = 79; i <= 102; i++)
			vm.on_condition(i, dynamics_bit);
		vm.on_procedure(198, set_dynamics_xy);
		vm.on_procedure(288, set_dynamics_xy);
		vm.on_procedure(102, show_parts);
		vm.on_procedure(103, show_parts);
		vm.on_condition(65, bits_overlap);
		vm.on_condition(72, bits_overlap);
		vm.on_condition(74, inside_waypoint);
		vm.on_condition(78, state_flag);
		vm.on_condition(103, is_zero_vector);
		vm.on_condition(67, in_behaviour);
		vm.on_condition(68, in_behaviour);
		vm.on_condition(70, in_state);
		vm.on_condition(75, custom_bit);
		vm.on_condition(119, is_kind);
		vm.on_condition(120, is_kind);

		vm.on_function(0, position);
		for(std::uint32_t i : { 43, 44, 45, 46, 47, 48 })
			vm.on_function(i, hit_points);
		vm.on_function(10, distance);
		vm.on_function(3, distance_to);
		for(std::uint32_t i : { 2, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17 })
			vm.on_function(i, relative);
		for(std::uint32_t i : { 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 32, 33, 34, 35 })
			vm.on_function(i, float_math);
		vm.on_function(52, against_axes);
		vm.on_function(54, against_axes);
		vm.on_function(60, clock_function);
		vm.on_function(61, clock_function);
		vm.on_function(149, dot_product);
		for(std::uint32_t i : { 162, 164, 165 })
			vm.on_function(i, matrix_vector);
		for(std::uint32_t i = 180; i <= 185; i++)
			vm.on_function(i, vector_ops);
		vm.on_function(240, parent_actor);
		vm.on_function(31, absolute);
		vm.on_function(42, smooth);
		vm.on_function(62, frame_time);
		vm.on_function(150, vector_math);
		vm.on_function(151, vector_math);
		vm.on_function(163, minus_y_axis);
		vm.on_function(248, bit_array);
		vm.on_function(249, bit_array);

		vm.on_procedure(69, change_behaviour);
		// 42..47: the scripts' fog override (0x468B90), until the renderer takes it.
		for(std::uint32_t i : { 42, 43, 44, 45, 46, 47, 115, 116, 117, 118, 119, 505, 506, 507 })
			vm.on_procedure(i, not_yet);
		for(std::uint32_t i : { 176, 182, 188, 194, 201, 207, 280 })
			vm.on_procedure(i, set_dynamics_vector);
		vm.on_procedure(70, change_behaviours);
		vm.on_procedure(71, change_behaviours);
		for(std::uint32_t i : { 72, 73, 74, 75 })
			vm.on_procedure(i, change_state_procedure);
		vm.on_procedure(88, pending_signal);
		vm.on_procedure(139, remove_actor);
		vm.on_procedure(181, set_dynamics_xyz);
		for(std::uint32_t i = 244; i <= 261; i++)
			if(i != 255)
				vm.on_procedure(i, dynamics_flag);
		vm.on_procedure(312, super_object_flag);
		for(std::uint32_t i = 321; i <= 332; i++)
			vm.on_procedure(i, zone_mode);
		vm.on_procedure(341, brain_period);
		for(std::uint32_t i = 378; i <= 385; i++)
			vm.on_procedure(i, gauge);
		for(std::uint32_t i : { 396, 397, 398 })
			vm.on_procedure(i, capabilities);
		vm.on_procedure(200, teleport);
		vm.on_procedure(433, face_horizontal);
		vm.on_procedure(171, orient_from_vectors);
		vm.on_procedure(503, start_effect);
		vm.on_procedure(161, face_direction);
		for(std::uint32_t i : { 163, 164, 165 })
			vm.on_procedure(i, rotate_local);
		vm.on_procedure(173, set_scale);
		vm.on_procedure(235, clear_contacts);
		vm.on_procedure(236, clear_contacts);
		vm.on_meta_action(0, wait_time());
		vm.on_meta_action(1, change_state_and_wait());
		vm.on_meta_action(2, wait_animation());
		vm.on_procedure(174, set_custom_bits);
		vm.on_procedure(175, set_custom_bits);
	}
}
