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
		// (not identified yet); 503: start a particle effect; 505: an on-screen text slot.
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
		vm.on_function(10, distance);
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
		for(std::uint32_t i : { 42, 43, 44, 45, 46, 47, 115, 116, 117, 118, 119, 503, 505, 506, 507 })
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
		vm.on_procedure(174, set_custom_bits);
		vm.on_procedure(175, set_custom_bits);
	}
}
