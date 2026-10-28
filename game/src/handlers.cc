#include <openrayman/game/handlers.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/input.h>

#include <array>
#include <bit>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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

		// The super object a value names. Designer variables of the level's actors sometimes hold
		// the super object itself where a script would name the actor, so both are accepted.
		std::uint32_t super_object_value(interpreter& vm, const value& v)
		{
			if(vm.actor_of(v.word()))
				return v.word();
			return vm.super_object_of(actor_value(vm, v));
		}

		std::uint32_t next_arg(call& c, value& v)
		{
			c.args = c.vm.eval(c.self, c.args, v);
			return c.args;
		}

		// --- Conditions --------------------------------------------------------------------------

		// 0..3 (0x489B40): and, or (short-circuit: the second argument is skipped when it can't
		// matter), not, and exclusive or: both are evaluated, true when their values differ.
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
				break;
			}
			value b;
			next_arg(c, b);
			result = c.index == 3 ? b.word() != a.word() : b.word() != 0;
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

		// 70 (0x469710): switch the reflex behaviour. 71 (0x469760): switch both, normal first, each
		// passing 0x469600 a "don't restart" flag of 0.
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
				c.vm.change_behaviour(normal, first.word(), false);
			if(std::uint32_t reflex = m.u32(mind + mind_field::reflex))
				c.vm.change_behaviour(reflex, second.word(), false);
			return c.args;
		}

		// 72..75 (0x472290): change the actor's state (73 even to the state already asked for, 75
		// takes a second number: non-zero skips the transitions). A refused
		// change stops the script run (flag 2).
		std::uint32_t change_state_procedure(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value target, direct;
			next_arg(c, target);
			if(c.index == 75)
				next_arg(c, direct);
			std::uint32_t data = actor_part(c.vm, self, actor_field::data_3d);
			if(data && !change_state(c.vm.memory(), data, target.word(), c.index == 73, c.vm.clock(), (direct.word() & 0xFF) != 0))
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

		// 89 (0x4675A0, helper 0x4187E0): how fast the animation plays, as a percentage of the state's
		// own rate (state +0x29): 100 keeps it, 50 halves it, 200 doubles it. The game takes state
		// rate x number / 100 and ignores a result of 1 or less (0x41880D); otherwise that becomes
		// the playback rate (3D data +0xE0) with the clock re-based so the frame doesn't jump.
		std::uint32_t set_animation_speed(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value percent;
			next_arg(c, percent);
			live_memory& m = c.vm.memory();
			std::uint32_t data = actor_part(c.vm, self, actor_field::data_3d);
			std::uint32_t s = data ? m.u32(data + data_3d::current_state) : 0;
			if(!s)
				return c.args;
			// The state's rate is signed and the number is a plain integer (a float is truncated).
			std::int32_t rate = std::int8_t(m.u8(s + state::rate)) * percent.as_int() / 100;
			if(std::uint8_t(rate) > 1)
				set_playback_rate(m, data, std::uint8_t(rate));
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
		// 102..107 read and change them). What it measures isn't known yet.
		std::uint32_t gauge(call& c)
		{
			value v;
			next_arg(c, v);
			float x = v.as_float();
			interpreter::camera_globals& g = c.vm.camera();
			switch(c.index)
			{
			case 378: case 380: g.gauge = x; break;
			case 379: case 381: g.gauge_max = x; break;
			case 382: g.gauge = std::clamp(g.gauge + x, 0.0f, g.gauge_max); break;
			case 383: g.gauge_max = std::max(g.gauge_max + x, 0.0f); g.gauge = std::min(g.gauge, g.gauge_max); break;
			case 384: g.gauge = std::clamp(g.gauge - x, 0.0f, g.gauge_max); break;
			case 385: g.gauge_max = std::max(g.gauge_max - x, 0.0f); g.gauge = std::min(g.gauge, g.gauge_max); break;
			}
			return c.args;
		}

		// 102..107 (0x475F90): the same gauge pair. 102 and 103 return the value and the maximum;
		// 104/106 add or take away the argument from the value, kept between 0 and the maximum;
		// 105/107 do the same for the maximum, pulling the value down with it. Each returns what it
		// changed.
		std::uint32_t gauge_function(call& c, value& out)
		{
			interpreter::camera_globals& g = c.vm.camera();
			if(c.index == 102 || c.index == 103)
			{
				out = value::real(c.index == 102 ? g.gauge : g.gauge_max);
				return c.args;
			}
			value v;
			next_arg(c, v);
			float delta = (c.index == 104 || c.index == 105 ? 1.0f : -1.0f) * v.as_float();
			if(c.index == 104 || c.index == 106)
			{
				float changed = g.gauge + delta;
				g.gauge = changed > g.gauge_max ? g.gauge_max : changed < 0.0f ? 0.0f : changed;
				out = value::real(g.gauge);
			}
			else
			{
				float changed = g.gauge_max + delta;
				g.gauge_max = changed < 0.0f ? 0.0f : changed;
				g.gauge = std::min(g.gauge, g.gauge_max);
				out = value::real(g.gauge_max);
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

		// 177, 178, 179 (0x46CE6A, 0x46CE9A, 0x46CEC9): one axis of procedure 176's vector (x, y,
		// z), in the actor's own axes: the float as given, the axis bit on, the world bit off. 180
		// (0x46CEF8) sets x and y. Rayman's jump is 179 at 12 units per second, every tick.
		std::uint32_t set_dynamics_axis(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value x, y;
			next_arg(c, x);
			if(c.index == 180)
				next_arg(c, y);
			std::uint32_t base = dynamics_base(c, self, 0x30);
			if(!base)
				return c.args;
			live_memory& m = c.vm.memory();
			std::uint32_t axis = c.index == 180 ? 0 : c.index - 177;
			m.write<std::uint32_t>(base + 0x24 + axis * 4, x.word());
			if(c.index == 180)
				m.write<std::uint32_t>(base + 0x28, y.word());
			std::uint32_t bits = c.index == 180 ? 0x60000u : 0x20000u << axis;
			m.write<std::uint32_t>(base + 8, (m.u32(base + 8) | bits) & ~0x4000u);
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
		// The axes are normalised, as the physics does when it turns a local speed back into the
		// world (mechanics.cc), so the two stay each other's inverse even when the axes have
		// drifted from unit length.
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

		// --- Zone tests ----------------------------------------------------------------------------
		// An actor's collision set (actor +0x14) keeps one zone list per kind (+0x00..+0x0C), one
		// group list per kind (+0x20, +0x28, +0x24, +0x2C) and four words of 2-bit zone states
		// (+0x30 kind 0, +0x3C kind 1, +0x34 kind 2, +0x38 kind 3; 0x417BC0, 0x417C10). A list
		// node is a next pointer (+0), then either the zone's 3D object (+4) in a kind list or the
		// zone's number (u16 at +4) in a group list. A zone is such a 3D object: +0 its vertices,
		// +0x10 its elements' kinds, +0x14 their data and +0x2e how many (0x48F3B0).

		std::uint32_t safe_u32(const live_memory& m, std::uint32_t at) { return m.contains(at, 4) ? m.u32(at) : 0; }
		std::uint16_t safe_u16(const live_memory& m, std::uint32_t at) { return m.contains(at, 2) ? m.read<std::uint16_t>(at) : 0; }
		std::int16_t safe_i16(const live_memory& m, std::uint32_t at) { return m.contains(at, 2) ? m.read<std::int16_t>(at) : 0; }
		float safe_f32(const live_memory& m, std::uint32_t at) { return m.contains(at, 4) ? m.f32(at) : 0.0f; }

		std::uint32_t collision_set(interpreter& vm, std::uint32_t super_object)
		{
			live_memory& m = vm.memory();
			std::uint32_t actor = vm.actor_of(super_object);
			return actor && m.contains(actor + actor_field::collision, 4) ? m.u32(actor + actor_field::collision) : 0;
		}

		// 0x417BC0: the header of one kind's group list (0 = none); 0x417C10: the kind's zone states.
		std::uint32_t zone_group_of(interpreter& vm, std::uint32_t set, std::uint32_t kind)
		{
			static constexpr std::uint32_t offsets[] = { 0x20, 0x28, 0x24, 0x2C };
			return kind < 4 ? safe_u32(vm.memory(), set + offsets[kind]) : 0;
		}
		std::uint32_t zone_states_of(interpreter& vm, std::uint32_t set, std::uint32_t kind)
		{
			static constexpr std::uint32_t offsets[] = { 0x30, 0x3C, 0x34, 0x38 };
			return kind < 4 ? safe_u32(vm.memory(), set + offsets[kind]) : 0;
		}

		// 0x417D60: zone `index`'s 2-bit state. 0 means "not on by the state word", 3 that the
		// actor has no collision set at all.
		std::uint32_t zone_state(interpreter& vm, std::uint32_t set, std::uint32_t kind, std::uint32_t index)
		{
			if(!set || index > 15)
				return 3;
			return zone_states_of(vm, set, kind) >> (2 * index) & 3;
		}

		// 0x40CC70: the zone at `index` of a kind's list: the list's first node, then `index` next
		// pointers, and the zone object is that node's +4.
		std::uint32_t zone_at(interpreter& vm, std::uint32_t set, std::uint32_t kind, std::uint32_t index)
		{
			live_memory& m = vm.memory();
			std::uint32_t head = set && kind < 4 ? safe_u32(m, set + kind * 4) : 0;
			std::uint32_t node = head ? safe_u32(m, head) : 0;
			for(std::uint32_t i = 0; i < index && node; i++)
				node = safe_u32(m, node);
			return node ? safe_u32(m, node + 4) : 0;
		}

		// 0x40C700: zone `index` (from 0) of kind `kind` of an actor (a 3D object), or 0. State 1
		// means the kind's list holds it; state 0 only when the kind's group list names that zone
		// number. Other states leave the zone out.
		std::uint32_t find_zone(interpreter& vm, std::uint32_t super_object, std::uint32_t kind, std::uint32_t index)
		{
			live_memory& m = vm.memory();
			std::uint32_t set = collision_set(vm, super_object);
			if(!set)
				return 0;
			std::uint32_t state = zone_state(vm, set, kind, index);
			if(state == 1)
				return zone_at(vm, set, kind, index);
			if(state != 0)
				return 0;
			std::uint32_t group = zone_group_of(vm, set, kind);
			if(!group)
				return 0;
			std::uint32_t node = safe_u32(m, group);
			std::uint32_t count = safe_u32(m, group + 8);
			for(std::uint32_t i = 0; node && i < count && safe_u16(m, node + 4) < index; i++)
				node = safe_u32(m, node);
			if(!node || safe_u16(m, node + 4) != index)
				return 0;
			return zone_at(vm, set, kind, index);
		}

		// 0x48F3B0: is a point, given in the zone's own axes, inside one of the zone's collision
		// elements? The element kinds are 1 triangles (like the level's collision), 7 spheres (a
		// vertex index and a radius each), 8 boxes (two vertex indices) and 9 capsules; 2..6 never
		// hold a point. Only 7 and 8 are read here, which is what the camera zones use; the tests
		// for 1 and 9 (0x491250, 0x492250) are their own geometry and are left out.
		bool zone_contains(interpreter& vm, std::uint32_t zone, const vec3& p)
		{
			live_memory& m = vm.memory();
			if(!zone || !m.contains(zone, 0x30))
				return false;
			std::uint32_t vertices = safe_u32(m, zone);
			std::uint32_t kinds = safe_u32(m, zone + 0x10);
			std::uint32_t data = safe_u32(m, zone + 0x14);
			std::uint16_t elements = safe_u16(m, zone + 0x2E);
			for(std::uint16_t i = 0; i < elements; i++)
			{
				std::uint32_t element = data ? safe_u32(m, data + std::uint32_t(i) * 4) : 0;
				if(!element)
					continue;
				std::int16_t kind = kinds ? safe_i16(m, kinds + std::uint32_t(i) * 2) : 0;
				std::uint32_t entries = safe_u32(m, element);
				std::uint16_t count = safe_u16(m, element + 4);
				if(!entries)
					continue;
				for(std::uint16_t j = 0; j < count; j++)
				{
					if(kind == 7)
					{
						// 0x4915E0: a sphere around a vertex, radius from the table.
						float radius = safe_f32(m, entries + std::uint32_t(j) * 12);
						std::int16_t at = safe_i16(m, entries + std::uint32_t(j) * 12 + 8);
						std::uint32_t centre = vertices + std::uint32_t(at) * 12;
						if(vertices && m.contains(centre, 12))
						{
							vec3 d = sub(p, read3(m, centre));
							if(dot(d, d) <= radius * radius)
								return true;
						}
					}
					else if(kind == 8)
					{
						// 0x490510: the axis-aligned box between two vertices.
						std::int16_t a = safe_i16(m, entries + std::uint32_t(j) * 8);
						std::int16_t b = safe_i16(m, entries + std::uint32_t(j) * 8 + 2);
						std::uint32_t first = vertices + std::uint32_t(a) * 12, second = vertices + std::uint32_t(b) * 12;
						if(!vertices || a < 0 || b < 0 || !m.contains(first, 12) || !m.contains(second, 12))
							continue;
						vec3 va = read3(m, first);
						vec3 vb = read3(m, second);
						if(p[0] >= std::min(va[0], vb[0]) && p[0] <= std::max(va[0], vb[0])
							&& p[1] >= std::min(va[1], vb[1]) && p[1] <= std::max(va[1], vb[1])
							&& p[2] >= std::min(va[2], vb[2]) && p[2] <= std::max(va[2], vb[2]))
							return true;
					}
				}
			}
			return false;
		}

		// A world point in a transform's own axes and origin: the inverse transform 0x444D60 builds
		// and 0x444800 applies. The rows at +0x10 are the axes, so the inverse is their dot products.
		vec3 in_frame(live_memory& m, std::uint32_t transform, const vec3& world)
		{
			if(!transform || !m.contains(transform, 0x58))
				return world;
			vec3 d = sub(world, read3(m, transform + 4)), local{};
			for(int i = 0; i < 3; i++)
				local[std::size_t(i)] = dot(d, read3(m, transform + 0x10 + std::uint32_t(i) * 12));
			return local;
		}

		// The engine sets standard-game +0x29 bit 2 when it puts an actor into the world (0x40614F);
		// ours never does, so the flag test would hide every actor. Left out for now.
		constexpr bool world_flag_always_set = true;

		// 0x40C960: is the other actor's position inside this zone? Only actors whose standard game
		// +0x29 has bit 2 set are tested at all.
		bool zone_holds(interpreter& vm, std::uint32_t zone, std::uint32_t transform, std::uint32_t other)
		{
			live_memory& m = vm.memory();
			std::uint32_t actor = vm.actor_of(other);
			std::uint32_t standard = actor && m.contains(actor + actor_field::standard_game, 4) ? m.u32(actor + actor_field::standard_game) : 0;
			std::uint8_t sflags = standard && m.contains(standard + standard_game_field::flags) ? m.u8(standard + standard_game_field::flags) : 0;
			if(!standard || (!world_flag_always_set && !(sflags & 4)))
				return false;
			return zone_contains(vm, zone, in_frame(m, transform, position_of(vm, other)));
		}

		// 0x40D290: zone `number` (from 1) of the owner's kind-`kind` zone table, and the transform
		// its elements are written in. Kind 0 is the collision set's own list, in the owner's frame.
		// The other kinds read the 20-byte-entry table at the 3D data's +0x90 (an entry's object
		// gives the 3D object, the entry's +0x24 the frame); an entry without byte +0 bits 3 and 4
		// comes from the second half of the table (index + 0x8000).
		std::uint32_t find_zone_with_frame(interpreter& vm, std::uint32_t owner, std::uint32_t kind, std::uint32_t number, std::uint32_t& frame)
		{
			frame = 0;
			if(number == 0)
				return 0;
			if(kind == 0)
			{
				std::uint32_t zone = find_zone(vm, owner, 0, number - 1);
				if(zone)
					frame = transform_of(vm, owner);
				return zone;
			}
			live_memory& m = vm.memory();
			std::uint32_t actor = vm.actor_of(owner);
			std::uint32_t data = actor ? safe_u32(m, actor) : 0;
			std::uint32_t table = data ? safe_u32(m, data + 0x90) : 0;
			if(!table)
				return 0;
			auto entry_at = [&](std::uint32_t index) {
				std::uint32_t at = table + index * 20;
				return m.contains(at, 20) ? m.u32(at) : 0;
			};
			std::uint32_t entry = entry_at(number & 0xFF);
			if(!entry)
				return 0;
			if(!(m.contains(entry) && (m.u8(entry) & 0x18)))
			{
				entry = entry_at((number + 0x8000) & 0xFFFF);
				if(!entry)
					return 0;
			}
			// 0x42DE30 twice: the entry's object, then its actor (0x444790 picks the list by kind).
			std::uint32_t object = safe_u32(m, entry + 4);
			std::uint32_t target = object ? safe_u32(m, object + 4) : 0;
			std::uint32_t zone = 0;
			if(target)
				zone = kind == 1 ? safe_u32(m, target) : kind == 2 ? safe_u32(m, target + 0xC) : safe_u32(m, target + 8);
			if(!zone)
				return 0;
			frame = safe_u32(m, entry + 0x24);
			return zone;
		}

		// Every zone of the owner's kind-0 list that holds the other actor. Condition 12 asks this;
		// the exe also skips zones whose group number says they are switched off, which needs the
		// group list's running state and is left out (nothing in Learn_10 uses it).
		bool inside_any_zone(interpreter& vm, std::uint32_t owner, std::uint32_t other)
		{
			live_memory& m = vm.memory();
			std::uint32_t set = collision_set(vm, owner);
			std::uint32_t head = set ? safe_u32(m, set) : 0;
			std::uint32_t count = head ? safe_u32(m, head + 8) : 0;
			std::uint32_t frame = transform_of(vm, owner);
			for(std::uint32_t i = 0; i < count; i++)
				if(std::uint32_t zone = zone_at(vm, set, 0, i))
					if(zone_holds(vm, zone, frame, other))
						return true;
			return false;
		}

		// 10..14 (0x489D60): zone tests. 10 asks whether B lies inside zone `index` of A's kind-0
		// table, 11 the same for kind 1; 12 whether B lies inside any of A's kind-0 zones; 13 and 14
		// ask the other way round, which actors lie inside zone `index` (kind 0 and 1), by looking
		// through the sectors' object lists (0x483870, 0x40D9D0) - lists our engine does not keep,
		// so 13 and 14 always answer false here.
		std::uint32_t actor_inside_zone(call& c, bool& result)
		{
			value who;
			next_arg(c, who);
			std::uint32_t a = super_object_value(c.vm, who);
			result = false;
			if(c.index == 12)
			{
				value other;
				next_arg(c, other);
				std::uint32_t b = super_object_value(c.vm, other);
				if(a && b)
					result = inside_any_zone(c.vm, a, b);
				return c.args;
			}
			value index_value;
			next_arg(c, index_value);
			std::uint32_t number = std::uint32_t(index_value.as_int()) & 0xFF;
			if(c.index == 13 || c.index == 14)
				return c.args;
			value other;
			next_arg(c, other);
			std::uint32_t b = super_object_value(c.vm, other);
			result = false;
			if(a && b)
			{
				std::uint32_t frame = 0;
				std::uint32_t zone = find_zone_with_frame(c.vm, a, c.index == 11 ? 1 : 0, number, frame);
				result = zone && zone_holds(c.vm, zone, frame, b);
			}
			return c.args;
		}

		// --- The script camera ---------------------------------------------------------------------
		// A camera object (the target actor's +0x10) points at four 0x7C-byte parameter blocks:
		// +0x00 the default values, +0x04 the current ones, +0x08 a spare copy and +0x0C the
		// temporary override the scripts fill each frame. cam+0x74 bit 0 says the override holds a
		// copy of the current block (0x4140A0), cam+0x75 bit 1 that the spare was saved.

		namespace camera_field
		{
			constexpr std::uint32_t block_default = 0x00, block_current = 0x04, block_spare = 0x08, block_override = 0x0C;
			constexpr std::uint32_t block_size = 0x7C;
			constexpr std::uint32_t override_active = 0x74; // u8, bit 0
			constexpr std::uint32_t snap = 0x17C;           // u8: the next update is an instant cut
		}

		// The camera object of an actor's super object, or 0 when it has none.
		std::uint32_t camera_object(interpreter& vm, std::uint32_t super_object)
		{
			live_memory& m = vm.memory();
			std::uint32_t actor = vm.actor_of(super_object);
			std::uint32_t cam = actor && m.contains(actor + 0x10, 4) ? m.u32(actor + 0x10) : 0;
			return cam && m.contains(cam, 0x80) ? cam : 0;
		}

		// One of those parameter blocks (an offset from camera_field above), or 0 when it isn't there.
		std::uint32_t camera_block(interpreter& vm, std::uint32_t cam, std::uint32_t which)
		{
			live_memory& m = vm.memory();
			std::uint32_t block = cam && m.contains(cam + which, 4) ? m.u32(cam + which) : 0;
			return block && m.contains(block, camera_field::block_size) ? block : 0;
		}

		// 0x4140A0: copy the current block into the override block unless that already holds the
		// copy, and remember that it does (cam+0x74 bit 0). Scripts edit the override every frame.
		void back_up_camera_block(interpreter& vm, std::uint32_t cam)
		{
			live_memory& m = vm.memory();
			std::uint8_t flags = m.u8(cam + camera_field::override_active);
			if(flags & 1)
				return;
			std::uint32_t from = camera_block(vm, cam, camera_field::block_current);
			std::uint32_t to = camera_block(vm, cam, camera_field::block_override);
			if(from && to)
				std::memcpy(m.span(to, camera_field::block_size).data(), m.span(from, camera_field::block_size).data(), camera_field::block_size);
			m.write<std::uint8_t>(cam + camera_field::override_active, std::uint8_t(flags | 1));
		}

		// 0x4733A0: set (value != 0) or clear the bits of `mask` in the 16-bit option word at
		// `offset` of the live blocks. mode_temp == 1 changes only the override block (after the
		// backup); otherwise the current, spare and override blocks all change.
		void set_camera_flags(interpreter& vm, std::uint32_t cam, std::uint32_t offset, std::int32_t mode_temp, std::int32_t value, std::uint32_t mask)
		{
			live_memory& m = vm.memory();
			std::uint32_t current = camera_block(vm, cam, camera_field::block_current);
			std::uint32_t spare = camera_block(vm, cam, camera_field::block_spare);
			std::uint32_t override_ = camera_block(vm, cam, camera_field::block_override);
			if(!current || !spare || !override_)
				return;
			if(mode_temp == 1)
				back_up_camera_block(vm, cam);
			auto apply = [&](std::uint32_t block) {
				std::uint16_t word = m.read<std::uint16_t>(block + offset), bits = std::uint16_t(mask);
				m.write<std::uint16_t>(block + offset, value ? std::uint16_t(word | bits) : std::uint16_t(word & ~bits));
			};
			if(mode_temp == 1)
			{
				apply(override_);
				return;
			}
			apply(current);
			apply(spare);
			apply(override_);
		}

		// 0x47DBE0: one float of the parameter blocks. mode_temp == 1 writes only the override block
		// (after the backup); otherwise the current, spare and override blocks all get it. mode -1
		// takes the default block's value, 0 adds `value` to the one in force (the override while it
		// is active, else the current one), 1 stores `value`.
		void set_camera_float(interpreter& vm, std::uint32_t cam, std::uint32_t offset, std::int32_t mode_temp, std::int32_t mode, float value)
		{
			live_memory& m = vm.memory();
			std::uint32_t current = camera_block(vm, cam, camera_field::block_current);
			std::uint32_t spare = camera_block(vm, cam, camera_field::block_spare);
			std::uint32_t override_ = camera_block(vm, cam, camera_field::block_override);
			if(!current || !spare || !override_)
				return;
			if(mode_temp == 1)
				back_up_camera_block(vm, cam);
			std::uint32_t written = mode_temp == 1 ? override_ : current;
			float result = 0;
			if(mode == -1)
			{
				if(std::uint32_t at_default = camera_block(vm, cam, camera_field::block_default))
					result = m.f32(at_default + offset);
			}
			else if(mode == 0)
				result = m.f32((m.u8(cam + camera_field::override_active) & 1 ? override_ : current) + offset) + value;
			else if(mode == 1)
				result = value;
			m.write<float>(written + offset, result);
			if(mode_temp != 1)
			{
				m.write<float>(spare + offset, result);
				m.write<float>(override_ + offset, result);
			}
		}

		// 0x47DCE0: the same for one vector of the parameter blocks.
		void set_camera_vector(interpreter& vm, std::uint32_t cam, std::uint32_t offset, std::int32_t mode_temp, std::int32_t mode, const vec3& value)
		{
			live_memory& m = vm.memory();
			std::uint32_t current = camera_block(vm, cam, camera_field::block_current);
			std::uint32_t spare = camera_block(vm, cam, camera_field::block_spare);
			std::uint32_t override_ = camera_block(vm, cam, camera_field::block_override);
			if(!current || !spare || !override_)
				return;
			if(mode_temp == 1)
				back_up_camera_block(vm, cam);
			std::uint32_t written = mode_temp == 1 ? override_ : current;
			vec3 result{};
			if(mode == -1)
			{
				if(std::uint32_t at_default = camera_block(vm, cam, camera_field::block_default))
					result = read3(m, at_default + offset);
			}
			else if(mode == 0)
			{
				vec3 base = read3(m, (m.u8(cam + camera_field::override_active) & 1 ? override_ : current) + offset);
				result = { base[0] + value[0], base[1] + value[1], base[2] + value[2] };
			}
			else if(mode == 1)
				result = value;
			write3(m, written + offset, result);
			if(mode_temp != 1)
			{
				write3(m, spare + offset, result);
				write3(m, override_ + offset, result);
			}
		}

		// 445, 446 (0x47DDE0): one of the camera's two vectors: 445 +0x00 and 446 +0x0C. Args:
		// mode_temp, mode, vector.
		std::uint32_t set_camera_vector_procedure(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value mode_temp, mode, v;
			next_arg(c, mode_temp);
			next_arg(c, mode);
			next_arg(c, v);
			if(std::uint32_t cam = camera_object(c.vm, self))
				set_camera_vector(c.vm, cam, c.index == 445 ? 0x00 : 0x0C, mode_temp.as_int(), mode.as_int(), vec_of(v));
			return c.args;
		}

		// 447..466 (0x47DEB0): one float of the camera's parameter blocks each, 447 at +0x18 up to
		// 466 at +0x64. Args: mode_temp, mode, value (the rules set_camera_float gives).
		std::uint32_t set_camera_parameter(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value mode_temp, mode, v;
			next_arg(c, mode_temp);
			next_arg(c, mode);
			next_arg(c, v);
			if(std::uint32_t cam = camera_object(c.vm, self))
				set_camera_float(c.vm, cam, 0x18 + (c.index - 447) * 4, mode_temp.as_int(), mode.as_int(), v.as_float());
			return c.args;
		}

		// The two option-word setters' work: set or clear `mask`'s bits of the 16-bit word at
		// `offset`, or with `whole` take the default block's word as the mask. value -1 copies the
		// bit at `shift` from the default block. Args: mode_temp, value.
		std::uint32_t set_camera_option_word(call& c, std::uint32_t offset, bool whole, std::uint32_t shift)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value mode_temp, v;
			next_arg(c, mode_temp);
			next_arg(c, v);
			live_memory& m = c.vm.memory();
			std::uint32_t cam = camera_object(c.vm, self);
			if(!cam)
				return c.args;
			std::uint32_t def = camera_block(c.vm, cam, camera_field::block_default);
			std::uint32_t mask = whole ? (def ? m.read<std::uint16_t>(def + offset) : 0) : 1u << shift;
			std::int32_t value = v.as_int();
			if(!whole && value == -1)
				value = def ? (m.read<std::uint16_t>(def + offset) >> shift & 1) : 0;
			set_camera_flags(c.vm, cam, offset, mode_temp.as_int(), value, mask);
			return c.args;
		}

		// 472..480 (0x47E670): one bit of the block +0x72 option word, 473 bit 0 up to 480 bit 7.
		// 472 takes the default block's whole word as the mask, i.e. "apply the default options".
		std::uint32_t set_camera_options_72(call& c)
		{
			static constexpr std::uint32_t shifts[] = { 0, 1, 2, 3, 4, 5, 6, 7 }; // 473..480
			bool whole = c.index == 472;
			return set_camera_option_word(c, 0x72, whole, whole ? 0 : shifts[c.index - 473]);
		}

		// 481..490 (0x47E830): the same for the block +0x70 option word, bits 0..3, 4, 5, 7, 6 and 8
		// (488 is bit 7 and 489 bit 6 in the exe's order). 481 takes the default block's whole word.
		std::uint32_t set_camera_options_70(call& c)
		{
			static constexpr std::uint32_t shifts[] = { 0, 1, 2, 3, 4, 5, 7, 6, 8 }; // 482..490
			bool whole = c.index == 481;
			return set_camera_option_word(c, 0x70, whole, whole ? 0 : shifts[c.index - 482]);
		}

		// 495..497 (0x47EC20): set or clear bit 0x1000 (495), 0x2000 (496) or 0x4000 (497) of the
		// block +0x72 word and, when value == 1, store the vectors straight into the camera object
		// (495 +0x28, 496 +0x34, 497 +0x40 then +0x4C). Args: mode_temp, value, vector [, vector].
		std::uint32_t set_camera_fixed_vector(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value mode_temp, on, first, second;
			next_arg(c, mode_temp);
			next_arg(c, on);
			next_arg(c, first);
			if(c.index == 497)
				next_arg(c, second);
			live_memory& m = c.vm.memory();
			std::uint32_t cam = camera_object(c.vm, self);
			if(!cam)
				return c.args;
			if(on.as_int() == 1)
			{
				write3(m, cam + (c.index == 495 ? 0x28 : c.index == 496 ? 0x34 : 0x40), vec_of(first));
				if(c.index == 497)
					write3(m, cam + 0x4C, vec_of(second));
			}
			std::uint32_t mask = c.index == 495 ? 0x1000 : c.index == 496 ? 0x2000 : 0x4000;
			set_camera_flags(c.vm, cam, 0x72, mode_temp.as_int(), on.as_int(), mask);
			return c.args;
		}

		// 467 (0x47E200): the camera's target, the super object at block +0x68. Args: mode_temp,
		// mode, actor. mode -1 takes the default block's target, 0 names nobody, anything else the
		// actor's super object (0x4136D0). mode_temp == 1 writes the override block only (after the
		// backup); otherwise the current, spare and override blocks all get it. The exe resets the
		// camera when the target changes (0x413E30, which zeroes the camera object's runtime
		// scratch, compared against the global 0x4B9C14); our carried state lives in camera_state,
		// and a new target that the current block did not already name starts it over.
		// A target set in the override block lasts one frame (444 refills it from the current block),
		// so scripts that want the camera for a while name it every frame; only a lasting change
		// starts the camera over.
		std::uint32_t set_camera_target(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value mode_temp, mode, who;
			next_arg(c, mode_temp);
			next_arg(c, mode);
			next_arg(c, who);
			live_memory& m = c.vm.memory();
			std::uint32_t cam = camera_object(c.vm, self);
			if(!cam)
				return c.args;
			std::uint32_t target = super_object_value(c.vm, who);
			if(mode.as_int() == -1)
			{
				std::uint32_t at_default = camera_block(c.vm, cam, camera_field::block_default);
				target = at_default ? safe_u32(m, at_default + 0x68) : 0;
			}
			else if(mode.as_int() == 0)
				target = 0;
			std::uint32_t current = camera_block(c.vm, cam, camera_field::block_current);
			std::uint32_t previous = current ? safe_u32(m, current + 0x68) : 0;
			if(mode_temp.as_int() == 1)
				back_up_camera_block(c.vm, cam);
			std::uint32_t written = camera_block(c.vm, cam, mode_temp.as_int() == 1 ? camera_field::block_override : camera_field::block_current);
			if(written)
				m.write<std::uint32_t>(written + 0x68, target);
			if(mode_temp.as_int() != 1)
				for(std::uint32_t which : { camera_field::block_spare, camera_field::block_override })
					if(std::uint32_t block = camera_block(c.vm, cam, which))
						m.write<std::uint32_t>(block + 0x68, target);
			if(previous != target && mode_temp.as_int() != 1)
				c.vm.camera_motion().valid = false;
			return c.args;
		}

		// 159 (0x467CF0): start the camera's eight tracked points at the target's position, each with
		// the small weight 0.03 (camera +0x78 three floats each, +0xD8 the point, +0x138 the weight).
		// The exe only does it when the target's camera object is the main actor's camera; our own
		// solve does not track the eight points yet (docs/formats/camera.md), so this only keeps the
		// data the exe's solver would use up to date.
		std::uint32_t start_camera_points(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			std::uint32_t cam = camera_object(c.vm, self);
			if(!cam || cam != camera_object(c.vm, c.vm.main_actor()))
				return c.args;
			std::uint32_t transform = transform_of(c.vm, self);
			if(!transform || !m.contains(cam + 0x78, 0xC8) || !m.contains(cam + 0x138, 0x20))
				return c.args;
			vec3 at = read3(m, transform + 4);
			for(int i = 0; i < 8; i++)
			{
				std::uint32_t offsets = cam + 0x78 + std::uint32_t(i) * 12;
				write3(m, offsets, { 0, 0, 0 });
				write3(m, offsets + 0x60, at);
				m.write<float>(cam + 0x138 + std::uint32_t(i) * 4, 0.03f);
			}
			return c.args;
		}

		// 491 (0x47EA70): write one entry of the camera's global settings block. index < 0x11 is a
		// float at +0x30 + 4*index, 0x11..0x16 an int16 at +0x52 + 2*index, 0x1E..0x26 a float at
		// +0x08 + 4*index and 0x29..0x2B a float at +0x00 + 4*index. 0x27 sets the bits of int(value)
		// in the option word +0xB0, or clears them and the camera's override flag when bit 1 is set;
		// 0x28 only clears them. 0x2C sets the camera's snap flag (value != 0). Other indices do
		// nothing. Args: index, value.
		std::uint32_t set_camera_setting(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value index_value, float_value;
			next_arg(c, index_value);
			next_arg(c, float_value);
			std::int32_t index = index_value.as_int();
			float value = float_value.as_float();
			live_memory& m = c.vm.memory();
			interpreter::camera_globals& settings = c.vm.camera();
			auto read_word = [&](std::size_t at) {
				return std::uint16_t(settings.values[at] | settings.values[at + 1] << 8);
			};
			auto write_word = [&](std::size_t at, std::uint16_t word) {
				settings.values[at] = std::uint8_t(word);
				settings.values[at + 1] = std::uint8_t(word >> 8);
			};
			auto write_float = [&](std::size_t at, float f) {
				std::memcpy(&settings.values[at], &f, sizeof(f));
			};
			if(index == 0x27 || index == 0x28)
			{
				std::uint32_t mask = std::uint32_t(std::int32_t(value));
				std::uint16_t word = read_word(0xB0);
				if(index == 0x27 && !(mask & 2))
					write_word(0xB0, std::uint16_t(word | mask));
				else
				{
					if(index == 0x27)
						if(std::uint32_t cam = camera_object(c.vm, self))
							m.write<std::uint8_t>(cam + camera_field::override_active, std::uint8_t(m.u8(cam + camera_field::override_active) & 0xFE));
					write_word(0xB0, std::uint16_t(word & ~mask));
				}
			}
			else if(index >= 0 && index < 0x11)
				write_float(0x30 + 4 * std::size_t(index), value);
			else if(index >= 0x11 && index < 0x17)
				write_word(0x52 + 2 * std::size_t(index), std::uint16_t(std::int32_t(value)));
			else if(index >= 0x1E && index < 0x27)
				write_float(0x08 + 4 * std::size_t(index), value);
			else if(index >= 0x29 && index < 0x2C)
				write_float(4 * std::size_t(index), value);
			else if(index == 0x2C)
				if(std::uint32_t cam = camera_object(c.vm, self))
					m.write<std::uint8_t>(cam + camera_field::snap, value != 0.0f ? 1 : 0);
			return c.args;
		}

		// 492, 493 (0x47EBD0): save the settings block from offset 0x34 on (0x4FF660..0x4FF6DF, 0x80
		// bytes) into the copy, and put it back. The camera model saves once and restores at the end
		// of every frame, so what 491 writes is per-frame scratch.
		std::uint32_t camera_settings_copy(call& c)
		{
			interpreter::camera_globals& settings = c.vm.camera();
			constexpr std::size_t at = 0x34, size = 0x80;
			if(c.index == 492)
				std::memcpy(settings.saved.data() + at, settings.values.data() + at, size);
			else
				std::memcpy(settings.values.data() + at, settings.saved.data() + at, size);
			return c.args;
		}

		// 444 (0x47DA40): commit the camera. The exe backs the current parameter block up into the
		// override one, runs the camera solve (0x485810) and applies its result to the camera's 3D
		// object (0x474420); our engine's camera code does the solve and the mechanics that moves it,
		// so the bookkeeping that touches the blocks is here: the backup, the solve (which reads the
		// override block), and clearing cam+0x74 afterwards.
		std::uint32_t camera_commit(call& c)
		{
			live_memory& m = c.vm.memory();
			if(std::uint32_t cam = camera_object(c.vm, c.self))
			{
				// The file's current block is empty: the game's camera set-up copies the level's
				// defaults into it when the camera object is made (the default block holds the
				// vector, the distances, the speeds). Our loader does not, so do it here once.
				std::uint32_t current = camera_block(c.vm, cam, camera_field::block_current);
				std::uint32_t at_default = camera_block(c.vm, cam, camera_field::block_default);
				if(current && at_default)
				{
					bool empty = true;
					for(std::uint32_t at = 0; at < camera_field::block_size && empty; at += 4)
						empty = m.u32(current + at) == 0;
					if(empty)
						std::memcpy(m.span(current, camera_field::block_size).data(), m.span(at_default, camera_field::block_size).data(), camera_field::block_size);
					// The set-up also aims the main camera at the main actor: the scripts only name
					// other targets for a frame at a time (in the override block, like the portal's
					// 467 while Rayman comes out), and the camera goes back to him after.
					if(!m.u32(current + 0x68) && !c.vm.camera_motion().valid)
						m.write<std::uint32_t>(current + 0x68, c.vm.main_actor());
				}
				back_up_camera_block(c.vm, cam);
				if(!c.vm.camera_motion().valid)
				{
					// 0x4858F6: a camera that has not been placed yet starts over and snaps (+0x70 =
					// 0x55, mode 4 in cam+0x60). Our mechanics takes bit 0 as the snap.
					if(std::uint32_t block = camera_block(c.vm, cam, camera_field::block_override))
						m.write<std::uint16_t>(block + 0x70, std::uint16_t(m.read<std::uint16_t>(block + 0x70) | 5));
				}
				solve_camera(c.vm, c.self);
				m.write<std::uint8_t>(cam + camera_field::override_active, 0);
			}
			return c.args;
		}

		// 499 (0x47EDD0): aim the camera at the target: cam+0x34 takes its position and bit 0x2000 of
		// the +0x72 word is set (in the override block only). With the snap flag (cam+0x17C) set it
		// is an instant cut instead: the exe backs the block up and runs the camera pipeline right
		// here (the engine's camera code does that), sets bits 0 and 2 of the current block's +0x70
		// and clears the flag.
		std::uint32_t camera_focus(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			std::uint32_t cam = camera_object(c.vm, self);
			if(!cam || !m.contains(cam + camera_field::snap))
				return c.args;
			if(!m.u8(cam + camera_field::snap))
			{
				write3(m, cam + 0x34, position_of(c.vm, self));
				set_camera_flags(c.vm, cam, 0x72, 1, 1, 0x2000);
				return c.args;
			}
			back_up_camera_block(c.vm, cam);
			std::uint32_t at_default = camera_block(c.vm, cam, camera_field::block_default);
			if(at_default && m.u32(at_default + 0x68))
			{
				if(std::uint32_t current = camera_block(c.vm, cam, camera_field::block_current))
					m.write<std::uint16_t>(current + 0x70, std::uint16_t(m.read<std::uint16_t>(current + 0x70) | 5));
				m.write<std::uint8_t>(cam + camera_field::snap, 0);
			}
			return c.args;
		}

		// 500 (0x47EF20): restore the current parameter block from the spare one when the spare was
		// saved (cam+0x75 bit 1, which the copy clears), then clear cam+0x60 and cam+0x75 bit 3.
		std::uint32_t restore_camera_parameters(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			std::uint32_t cam = camera_object(c.vm, self);
			if(!cam)
				return c.args;
			if(m.u8(cam + 0x75) & 2)
			{
				std::uint32_t from = camera_block(c.vm, cam, camera_field::block_spare);
				std::uint32_t to = camera_block(c.vm, cam, camera_field::block_current);
				if(from && to)
					std::memcpy(m.span(to, camera_field::block_size).data(), m.span(from, camera_field::block_size).data(), camera_field::block_size);
				m.write<std::uint8_t>(cam + 0x75, std::uint8_t(m.u8(cam + 0x75) & 0xFD));
			}
			m.write<std::uint32_t>(cam + 0x60, 0);
			m.write<std::uint8_t>(cam + 0x75, std::uint8_t(m.u8(cam + 0x75) & 0xF7));
			return c.args;
		}

		// 310 (0x488CB0): the movement the camera would make this frame, without committing it.
		// The exe backs the current block up into the override block, builds the camera context,
		// turns bit 0 of its +0x72 option word on and bit 0x1000 of the current block's off, runs
		// the first two stages of the solve (0x483F90 the wanted position, 0x4840A0 the offset
		// point) and returns their translation (context +0xA0). Nothing is committed. Here the
		// solve our camera code does stands in for those stages, and its result (which includes
		// the distance clamp and the height limits of 0x485810) is put back afterwards so the
		// frame's commit is unaffected. Bit 0 of +0x72 means nothing to our solve; 0x1000 (the
		// script's fixed offset point) is cleared in the block the solve reads, as the exe does.
		std::uint32_t camera_move_vector(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			live_memory& m = c.vm.memory();
			out = value::vector(0, 0, 0);
			std::uint32_t cam = camera_object(c.vm, self);
			if(!cam)
				return c.args;
			back_up_camera_block(c.vm, cam);
			std::uint32_t block = camera_block(c.vm, cam, camera_field::block_override);
			std::uint16_t saved = block ? m.read<std::uint16_t>(block + 0x72) : 0;
			if(block)
				m.write<std::uint16_t>(block + 0x72, std::uint16_t((saved | 1) & ~0x1000u));
			camera_state before = c.vm.camera_motion();
			solve_camera(c.vm, self);
			out = value_of(c.vm.camera_motion().move);
			c.vm.camera_motion() = before;
			if(block)
				m.write<std::uint16_t>(block + 0x72, saved);
			return c.args;
		}

		// 186 (0x479BE0, shared with 187 and 188): the angle between two directions. Both are
		// normalised first; a first direction that is (nearly) zero gives 0 and the third argument
		// is skipped. 186 returns degrees, signed by the component of the two directions' cross
		// product that the third argument's low byte names (0 the x axis, 1 y, 2 z; anything else
		// keeps the angle positive). 187 returns the cosine instead. 188 (an axis-angle built with
		// the lookup table at 0x50DB80) is not read, so it is not registered.
		std::uint32_t direction_angle(call& c, value& out)
		{
			value first, second;
			next_arg(c, first);
			next_arg(c, second);
			vec3 a = vec_of(first), b = vec_of(second);
			auto tiny = [](const vec3& v) {
				return std::abs(v[0]) < 1e-5f && std::abs(v[1]) < 1e-5f && std::abs(v[2]) < 1e-5f;
			};
			if(tiny(a))
			{
				out = value::real(0);
				c.args = c.vm.skip(c.args);
				return c.args;
			}
			a = normalised(a);
			if(!tiny(b))
				b = normalised(b);
			if(c.index == 187)
			{
				out = value::real(std::max(-1.0f, std::min(1.0f, dot(a, b))));
				return c.args;
			}
			float cosine = std::max(-1.0f, std::min(1.0f, dot(a, b)));
			float angle = std::acos(cosine) * 57.29578f;
			value axis;
			next_arg(c, axis);
			float side = 0;
			switch(axis.word() & 0xFF)
			{
			case 0: side = b[1] * a[0] - b[0] * a[1]; break;
			case 1: side = b[2] * a[1] - b[1] * a[2]; break;
			case 2: side = b[0] * a[2] - b[2] * a[0]; break;
			}
			out = value::real(side < 0 ? -angle : angle);
			return c.args;
		}

		// 190 (0x4785B0, shared with 189 and 191): one of the six normals of the actor's contact
		// report (dynamics base +0x150 points at it; 0x482E20): argument 0..5 picks the 0x28-byte
		// entry, and its vector at +4 comes back (entry 1 is the ground, 2 a wall). Any other
		// argument gives a zero vector. (189 adds up the entries its flags name; not registered.)
		std::uint32_t contact_normal(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value which;
			next_arg(c, which);
			live_memory& m = c.vm.memory();
			std::uint32_t base = dynamics_base(c, self, 0x10);
			std::uint32_t report = base ? safe_u32(m, base + 0x150) : 0;
			std::int32_t n = which.as_int();
			vec3 normal{};
			if(report && n >= 0 && n <= 5 && m.contains(report + 0xC + std::uint32_t(n) * 0x28, 12))
				normal = read3(m, report + 0xC + std::uint32_t(n) * 0x28);
			out = value_of(normal);
			return c.args;
		}

		// The surface flags of the game material a contact slot names at +0x20 (0x479220: the game
		// material's collide material, its u16 at +2), 0 without one.
		std::uint32_t slot_surface(const live_memory& m, std::uint32_t report, std::uint32_t slot)
		{
			std::uint32_t material = safe_u32(m, report + 8 + slot * 0x28 + 0x20);
			if(material == 0 || material == 0xCDCDCDCD)
				return 0;
			std::uint32_t collide = safe_u32(m, material + 0xC);
			if(collide == 0 || collide == 0xFFFFFFFF || !m.contains(collide + 2, 2))
				return 0;
			return m.read<std::uint16_t>(collide + 2);
		}

		std::uint32_t contact_report(call& c)
		{
			std::uint32_t base = dynamics_base(c, c.vm.target(c.self, true), 0x10);
			return base ? safe_u32(c.vm.memory(), base + 0x150) : 0;
		}

		// 196, 197 (0x479260): the surface flags of the actor's contacts. 197: of slot 0..5 (any
		// other gives 0). 196: of every slot its contact word names, together (bit 0x1 ground, 0x4
		// slot 2, 0x10000 slot 3, 0x40 slot 4, 0x10 slot 5; a word of 0 means slot 0).
		std::uint32_t contact_surface(call& c, value& out)
		{
			value which;
			if(c.index == 197)
				next_arg(c, which);
			live_memory& m = c.vm.memory();
			std::uint32_t report = contact_report(c);
			std::uint32_t bits = 0;
			if(report && c.index == 197)
			{
				std::int32_t n = which.as_int();
				if(n >= 0 && n <= 5)
					bits = slot_surface(m, report, std::uint32_t(n));
			}
			else if(report)
			{
				std::uint32_t word = safe_u32(m, report + 4);
				const std::pair<std::uint32_t, std::uint32_t> slots[] = { { 0x10000, 3 }, { 0x1, 1 }, { 0x4, 2 }, { 0x40, 4 }, { 0x10, 5 } };
				for(auto [bit, slot] : slots)
					if(word & bit)
						bits |= slot_surface(m, report, slot);
				if(word == 0)
					bits |= slot_surface(m, report, 0);
			}
			out = value::of(value_type::mask, bits);
			return c.args;
		}

		// 61..64 (0x48B000): the surface of a contact has every bit of the mask. 61: any contact
		// (slot 0), 62: slot 2 (word bit 0x4), 63: the ground (slot 1, bit 0x1), 64: slot 5 (bit
		// 0x10). False without that contact.
		std::uint32_t contact_has_surface(call& c, bool& result)
		{
			value mask;
			next_arg(c, mask);
			result = false;
			std::uint32_t report = contact_report(c);
			if(!report)
				return c.args;
			std::uint32_t word = safe_u32(c.vm.memory(), report + 4);
			const std::uint32_t need[] = { 0, 0x4, 0x1, 0x10 }, slot[] = { 0, 2, 1, 5 };
			std::size_t k = std::size_t(c.index - 61);
			if(k > 3 || (k == 0 ? word == 0 : (word & need[k]) == 0))
				return c.args;
			std::uint32_t want = mask.word() & 0xFFFF;
			result = want != 0 && (slot_surface(c.vm.memory(), report, slot[k]) & want) == want;
			return c.args;
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
			// 0x432DA0: the dynamics forgets the speed it had (base +0x3C and +0x6C), so a held
			// actor doesn't gather a fall.
			if(std::uint32_t base = dynamics_base(c, so, 0x78))
				for(std::uint32_t at : { 0x3Cu, 0x40u, 0x44u, 0x6Cu, 0x70u, 0x74u })
					c.vm.memory().write<float>(base + at, 0.0f);
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

		// 166 (0x46AB20, case 0x46ACD1): turn about an axis given in the actor's own frame (any
		// length) by degrees: the axis goes to the world through the actor's rotation (0x4449A0),
		// then the actor turns about it, right-handed. The portal spins Rayman about its own Z axis
		// this way as he comes out (node 77: the axis is the portal's Z brought into Rayman's frame
		// by function 54, 0.5 degrees per millisecond). Our first version took the axis as a world
		// one, so it wobbled with his own turning.
		std::uint32_t rotate_about_axis(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value axis_value, degrees;
			next_arg(c, axis_value);
			next_arg(c, degrees);
			std::uint32_t t = transform_of(c.vm, self);
			vec3 local = vec_of(axis_value);
			if(!t || length(local) < 1e-9f)
				return c.args;
			live_memory& m = c.vm.memory();
			vec3 axes[3] = { read3(m, t + 0x10), read3(m, t + 0x1C), read3(m, t + 0x28) };
			vec3 k{};
			for(std::size_t i = 0; i < 3; i++)
				for(std::size_t j = 0; j < 3; j++)
					k[j] += local[i] * axes[i][j];
			if(length(k) < 1e-9f)
				return c.args;
			k = normalised(k);
			float a = degrees.as_float() * 0.0174533f, cs = std::cos(a), sn = std::sin(a);
			for(vec3& v : axes)
			{
				vec3 kv = cross(k, v);
				float along = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
				for(std::size_t i = 0; i < 3; i++)
					v[i] = v[i] * cs + kv[i] * sn + k[i] * along * (1 - cs);
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
		// 190 (0x46CFE1): the proposed speed's local Y (the actor's forward is -Y), its walking speed:
		// stored as given, without converting (fld), y bit on, world bit off.
		std::uint32_t set_forward_speed(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			std::uint32_t base = dynamics_base(c, self, 0x38);
			if(!base)
				return c.args;
			live_memory& m = c.vm.memory();
			m.write<std::uint32_t>(base + 0x34, v.word());
			m.write<std::uint32_t>(base + 8, (m.u32(base + 8) & ~0x8000u) | 0x200000);
			return c.args;
		}

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

		// 346 (0x46E450): the movement input. Turns the direction actions into the vector and numbers
		// scripts read as fields 11..17. Rayman calls it as "camera: procedure_346(...)", so the
		// running super object is Rayman and the target the camera: up moves along the camera's
		// view, down towards the camera, sideways across. Args: dead zone, turn threshold (degrees),
		// turn rate, smoothing, mode (0 release, 2 in the actor's own axes, other: the camera's),
		// turn (1 = turn the actor towards the input), direction code wanted. Joystick input (when
		// no key is held) isn't read yet. Each tick is one of the exe's reference frames, so its
		// frame exponent is 1 and smoothing factors apply once per tick.
		std::uint32_t read_movement(call& c)
		{
			std::uint32_t self = c.self;
			std::uint32_t target = c.vm.target(c.self, true);
			value v[7];
			for(value& a : v)
				next_arg(c, a);
			float dead_zone = v[0].as_float();
			float threshold = v[1].as_float() * 0.0174532924f;
			float rate = v[2].as_float();
			float smoothing = v[3].as_float();
			std::int32_t mode = v[4].as_int();
			std::uint32_t turn = v[5].word(), code = v[6].word();
			interpreter::movement_input& out = c.vm.movement;
			live_memory& m = c.vm.memory();

			std::int32_t h = 0, vert = 0;
			vec3 dir{};
			float raw = 0, smooth = 0, angle = 0;
			auto write = [&] {
				out.short_a = std::int16_t(h);
				out.short_b = std::int16_t(vert);
				out.vector = dir;
				out.real_a = smooth;
				out.real_b = raw;
				out.real_c = angle;
				out.integer = std::int32_t(code);
				return c.args;
			};
			if(mode == 0)
			{
				code = 0;
				return write();
			}

			// The keys: up -100, down +100, right +100, left -100; walking halves them; diagonals
			// keep the length; then eased towards last tick's values.
			auto held = [&](std::uint32_t action) { return action && m.read<std::int32_t>(action + input_action::counter) > 0; };
			float axis_h = 0, axis_v = 0;
			bool any = false;
			if(held(out.actions[0])) { axis_v = -100; any = true; }
			if(held(out.actions[1])) { axis_v += 100; any = true; }
			if(held(out.actions[3])) { axis_h = 100; any = true; }
			if(held(out.actions[2])) { axis_h -= 100; any = true; }
			if(any)
			{
				float curve = 0.6f;
				if(held(out.actions[4]))
				{
					curve = 0.95f;
					axis_h *= 0.5f;
					axis_v *= 0.5f;
				}
				if(axis_h != 0 && axis_v != 0)
				{
					axis_h *= 0.70710677f;
					axis_v *= 0.70710677f;
				}
				if(out.snap)
					out.snap = false;
				else
				{
					axis_h += (out.axis_h - axis_h) * curve;
					axis_v += (out.axis_v - axis_v) * curve;
				}
			}
			out.axis_h = axis_h;
			out.axis_v = axis_v;
			if(mode == 2)
			{
				rate = 0;
				turn = 0;
			}
			h = std::int32_t(axis_h);
			vert = std::int32_t(axis_v);

			// Length, dead zone, and the eased length (field 14).
			dir = mode == 2 ? vec3{ float(-h), 0, float(-vert) } : vec3{ float(-h), float(vert), 0 };
			raw = length(dir);
			if(raw <= dead_zone)
			{
				raw = 0;
				dir = {};
				turn = 0;
			}
			smooth = raw;
			if(smoothing > 0)
				smooth = raw + (out.real_a - raw) * smoothing;
			if(smooth - raw > dead_zone)
			{
				dir = {};
				turn = 0;
			}
			if(smooth < dead_zone)
			{
				h = vert = 0;
				dir = {};
				code = 0;
				return write();
			}

			std::uint32_t own = transform_of(c.vm, self), aim = transform_of(c.vm, target);
			if(!own || !aim)
				return write();
			auto axis = [&](std::uint32_t t, int k) { return read3(m, t + 0x10 + std::uint32_t(k) * 12); };
			auto zero = [](const vec3& a) { return a[0] == 0 && a[1] == 0 && a[2] == 0; };
			if(mode == 2)
			{
				// In the actor's own axes, leaning towards the target's.
				if(!zero(dir))
					dir = normalised(dir);
				dir = { dir[0] * smooth, dir[1] * smooth, dir[2] * smooth };
				auto lean = [](float d) {
					float q = std::min(1.0f, std::pow(std::abs(d), 0.3f) * 1.5f);
					return d == 0 ? 0.0f : std::copysign(q, d);
				};
				vec3 a0 = axis(own, 0), a2 = axis(own, 2);
				float q0 = lean(dot(axis(aim, 0), a0)), q1 = lean(dot(axis(aim, 1), a2));
				float x = q0 * dir[0], z = q1 * dir[2];
				dir = { a0[0] * x + a2[0] * z, a0[1] * x + a2[1] * z, a0[2] * x + a2[2] * z };
			}
			else
			{
				// Forward: from the actor towards the camera, flattened; pushing up (away from the
				// camera), or when the camera is right above or on the actor, the camera's own
				// view instead (its Y axis, flattened).
				vec3 to = normalised(sub(read3(m, aim + 4), read3(m, own + 4)));
				vec3 forward;
				if(zero(to) || vert < 0 || std::abs(to[2]) > 0.8f)
				{
					forward = axis(aim, 1);
					forward[2] = 0;
					if(zero(forward))
					{
						forward = axis(aim, 2);
						forward[2] = 0;
					}
				}
				else
				{
					forward = { to[0], to[1], 0 };
					if(zero(forward))
						forward = { axis(aim, 2)[0], axis(aim, 2)[1], 0 };
				}
				if(zero(forward))
					return write();
				forward = normalised(forward);
				vec3 right{ forward[1], -forward[0], 0 };
				dir = { -h * right[0] + vert * forward[0], -h * right[1] + vert * forward[1], 0 };
				if(zero(dir))
					return write();
				dir = normalised(dir);

				// The turn from where the actor faces (-Y) to the input, signed (left positive).
				vec3 facing = axis(own, 1);
				facing = { -facing[0], -facing[1], 0 };
				if(zero(facing))
					return write();
				facing = normalised(facing);
				float d = std::clamp(facing[0] * dir[0] + facing[1] * dir[1], -1.0f, 1.0f);
				angle = std::acos(d);
				if(facing[0] * dir[1] - facing[1] * dir[0] < 0)
					angle = -angle;
				dir = { dir[0] * smooth, dir[1] * smooth, dir[2] * smooth };

				// Turning the actor about Z, part of the way each tick.
				if(turn == 1 && rate != 0 && raw != 0)
				{
					float step = float(c.vm.frame_ms()) * 0.0001f * rate * angle * smooth;
					float by = std::abs(angle) < std::abs(step) ? angle : step;
					if(std::abs(by) > threshold)
					{
						float cs = std::cos(by), sn = std::sin(by);
						for(int k = 0; k < 3; k++)
						{
							vec3 a = axis(own, k), scaled = read3(m, own + 0x34 + std::uint32_t(k) * 12);
							vec3 turned{ a[0] * cs - a[1] * sn, a[0] * sn + a[1] * cs, a[2] };
							float scale = length(scaled) / std::max(length(a), 1e-9f);
							write3(m, own + 0x10 + std::uint32_t(k) * 12, turned);
							write3(m, own + 0x34 + std::uint32_t(k) * 12, { turned[0] * scale, turned[1] * scale, turned[2] * scale });
						}
					}
				}
				angle *= 57.29578f;
			}

			// Which way, as 1..4 (forward, right, back, left), if asked.
			if(code != 0)
			{
				if(mode == 2)
					code = h > vert ? (h > -vert ? 2 : 1) : (h <= -vert ? 4 : 3);
				else if(std::abs(angle) <= 45)
					code = 1;
				else if(std::abs(angle) >= 135)
					code = 3;
				else
					code = angle > 0 ? 4 : 2;
			}
			return write();
		}

		// 263 (0x46C710, case 0x46C78B): a dynamics value at base +0x10, stored as given. 274
		// (0x46CA40, case 0x46CB2B): the vector at base +0x178, but only its components the
		// argument makes positive. What the physics does with them isn't read yet.
		std::uint32_t set_dynamics_value(call& c)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			value v;
			next_arg(c, v);
			std::uint32_t base = dynamics_base(c, self, 0x184);
			if(!base)
				return c.args;
			live_memory& m = c.vm.memory();
			if(c.index == 263)
				m.write<float>(base + 0x10, v.as_float()); // 0x4825B0 converts the argument
			else
				for(int i = 0; i < 3; i++)
					if(float f = v.component(i); f > 0)
						m.write<float>(base + 0x178 + std::uint32_t(i) * 4, f);
			return c.args;
		}

		// 435 (0x46FAA0): the input actions procedure 346 moves by: up, down, left, right, walk.
		std::uint32_t set_movement_actions(call& c)
		{
			for(std::uint32_t& action : c.vm.movement.actions)
			{
				value v;
				next_arg(c, v);
				action = v.word();
			}
			return c.args;
		}

		// 221 (0x47A1B8): the actor's gravity (dynamics base +0x10); 0 without dynamics. Rayman's
		// scripts read it and set it back (macro 35), so without it his gravity became 0.
		std::uint32_t gravity_of(call& c, value& out)
		{
			std::uint32_t self = c.vm.target(c.self, true);
			std::uint32_t base = dynamics_base(c, self, 0x14);
			out = value::real(base ? c.vm.memory().f32(base + 0x10) : 0.0f);
			return c.args;
		}

		// 19 (0x47BCE0): a number as an integer (floats truncate). 20: a random integer between two
		// numbers, both included (0x449C10 reads a table of pre-made random numbers; any uniform
		// source does).
		std::uint32_t to_integer(call& c, value& out)
		{
			value a;
			next_arg(c, a);
			std::int32_t x = a.as_int();
			if(c.index == 20)
			{
				value b;
				next_arg(c, b);
				std::int32_t y = b.as_int();
				static std::uint32_t seed = 54321;
				seed = seed * 1664525u + 1013904223u;
				std::int64_t span = std::int64_t(y) - x + 1;
				x = span > 0 ? std::int32_t(x + std::int64_t(seed >> 8) * span / (1 << 24)) : x;
			}
			out = value::integer(x);
			return c.args;
		}

		// 36..41 (0x47BB70): one of two numbers, as a float. 36..40 compare two numbers first (<, >,
		// ==, <=, >=), 41 takes a condition. Only the chosen number is evaluated.
		std::uint32_t choose(call& c, value& out)
		{
			value a;
			next_arg(c, a);
			bool pick;
			if(c.index == 41)
				pick = (a.word() & 0xFF) != 0;
			else
			{
				value b;
				next_arg(c, b);
				float x = a.as_float(), y = b.as_float();
				bool r[] = { x < y, x > y, x == y, x <= y, x >= y };
				pick = r[c.index - 36];
			}
			value chosen;
			if(pick)
			{
				next_arg(c, chosen);
				c.args = c.vm.skip(c.args);
			}
			else
			{
				c.args = c.vm.skip(c.args);
				next_arg(c, chosen);
			}
			out = value::real(chosen.as_float());
			return c.args;
		}

		// 42, 43 (0x48AEA0): bit `n` (0..7) of the 3D data's marker byte, which animation events arm
		// for one tick with a point (the shot leaving Rayman's hand). 43 also hands the point to the
		// variable named by the node after its argument (only that node's index is read).
		std::uint32_t animation_marker(call& c, bool& result)
		{
			value n;
			next_arg(c, n);
			live_memory& m = c.vm.memory();
			std::uint32_t data = actor_part(c.vm, c.self, actor_field::data_3d);
			std::int32_t bit = n.as_int();
			result = data && bit >= 0 && bit <= 7 && (m.u8(data + data_3d::markers) >> bit & 1);
			if(c.index == 42)
				return c.args;
			if(!result)
				return c.args + 8;
			std::uint32_t at = data + data_3d::marker_point;
			return c.vm.assign(c.self, c.args, value::vector(m.f32(at), m.f32(at + 4), m.f32(at + 8)));
		}

		// 77 (0x48B620 -> 0x410310): the current animation has played to its end. 106 (-> 0x410340):
		// it ran out this tick (3D data +0x8F).
		std::uint32_t animation_done(call& c, bool& result)
		{
			std::uint32_t target = c.vm.target(c.self, true);
			if(c.index == 77)
				result = animation_finished(c.vm, target);
			else
			{
				std::uint32_t data = actor_part(c.vm, target, actor_field::data_3d);
				result = data && c.vm.memory().u8(data + data_3d::reached_end);
			}
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
		vm.on_condition(106, animation_done);
		vm.on_condition(42, animation_marker);
		vm.on_condition(43, animation_marker);
		for(std::uint32_t i = 79; i <= 102; i++)
			vm.on_condition(i, dynamics_bit);
		vm.on_procedure(198, set_dynamics_xy);
		vm.on_procedure(190, set_forward_speed);
		vm.on_procedure(288, set_dynamics_xy);
		vm.on_procedure(435, set_movement_actions);
		vm.on_procedure(346, read_movement);
		vm.on_procedure(263, set_dynamics_value);
		vm.on_procedure(274, set_dynamics_value);
		vm.on_procedure(102, show_parts);
		vm.on_procedure(103, show_parts);
		vm.on_condition(65, bits_overlap);
		for(std::uint32_t i = 61; i <= 64; i++)
			vm.on_condition(i, contact_has_surface);
		vm.on_function(196, contact_surface);
		vm.on_function(197, contact_surface);
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
		for(std::uint32_t i = 10; i <= 14; i++)
			vm.on_condition(i, actor_inside_zone);

		vm.on_function(0, position);
		for(std::uint32_t i : { 43, 44, 45, 46, 47, 48 })
			vm.on_function(i, hit_points);
		vm.on_function(10, distance);
		vm.on_function(19, to_integer);
		vm.on_function(221, gravity_of);
		vm.on_function(20, to_integer);
		for(std::uint32_t i = 36; i <= 41; i++)
			vm.on_function(i, choose);
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
		vm.on_function(186, direction_angle);
		vm.on_function(187, direction_angle);
		vm.on_function(190, contact_normal);
		vm.on_function(310, camera_move_vector);
		vm.on_function(31, absolute);
		vm.on_function(42, smooth);
		vm.on_function(62, frame_time);
		vm.on_function(150, vector_math);
		vm.on_function(151, vector_math);
		vm.on_function(163, minus_y_axis);
		vm.on_function(248, bit_array);
		vm.on_function(249, bit_array);
		for(std::uint32_t i = 102; i <= 107; i++)
			vm.on_function(i, gauge_function);

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
		vm.on_procedure(89, set_animation_speed);
		vm.on_procedure(139, remove_actor);
		for(std::uint32_t i : { 177, 178, 179, 180 })
			vm.on_procedure(i, set_dynamics_axis);
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
		vm.on_procedure(166, rotate_about_axis);
		vm.on_procedure(173, set_scale);
		vm.on_procedure(235, clear_contacts);
		vm.on_procedure(236, clear_contacts);
		vm.on_meta_action(0, wait_time());
		vm.on_meta_action(1, change_state_and_wait());
		vm.on_meta_action(2, wait_animation());
		vm.on_procedure(174, set_custom_bits);
		vm.on_procedure(175, set_custom_bits);
		// 444..500: the script camera's parameter blocks and its global settings.
		for(std::uint32_t i : { 445, 446 })
			vm.on_procedure(i, set_camera_vector_procedure);
		for(std::uint32_t i = 447; i <= 466; i++)
			vm.on_procedure(i, set_camera_parameter);
		for(std::uint32_t i = 472; i <= 480; i++)
			vm.on_procedure(i, set_camera_options_72);
		for(std::uint32_t i = 481; i <= 490; i++)
			vm.on_procedure(i, set_camera_options_70);
		for(std::uint32_t i = 495; i <= 497; i++)
			vm.on_procedure(i, set_camera_fixed_vector);
		vm.on_procedure(444, camera_commit);
		vm.on_procedure(467, set_camera_target);
		vm.on_procedure(159, start_camera_points);
		vm.on_procedure(491, set_camera_setting);
		vm.on_procedure(492, camera_settings_copy);
		vm.on_procedure(493, camera_settings_copy);
		vm.on_procedure(499, camera_focus);
		vm.on_procedure(500, restore_camera_parameters);
	}
}
