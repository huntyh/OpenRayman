#pragma once

#include <openrayman/game/camera.h>
#include <openrayman/game/live_memory.h>
#include <openrayman/game/mechanics.h>
#include <openrayman/game/particles.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace openrayman::game
{
	// What kind of value a node leaves behind, as the exe tags them (see docs/formats/scripts.md).
	namespace value_type
	{
		constexpr std::uint32_t integer = 0x06;
		constexpr std::uint32_t real = 0x08;
		constexpr std::uint32_t text = 0x09;
		constexpr std::uint32_t waypoint = 0x0A;
		constexpr std::uint32_t actor = 0x0B;         // an actor ("perso") address
		constexpr std::uint32_t buffer = 0x0C;        // an address inside a variable buffer
		constexpr std::uint32_t nothing = 0x14;       // statements; the first byte carries flags
		constexpr std::uint32_t vector = 0x15;
		constexpr std::uint32_t state = 0x18;
		constexpr std::uint32_t input = 0x19;
		constexpr std::uint32_t mask = 0x1A;
		constexpr std::uint32_t super_object = 0x1E;
		constexpr std::uint32_t family = 0x20;
		constexpr std::uint32_t behaviour = 0x22;
		constexpr std::uint32_t id = 0x24;
		constexpr std::uint32_t model = 0x28;
	}

	// The result of evaluating a node: up to three words, and their type.
	struct value
	{
		std::array<std::uint32_t, 3> words{};
		std::uint32_t type = value_type::nothing;

		static value integer(std::int32_t v) { return { { std::uint32_t(v), 0, 0 }, value_type::integer }; }
		static value real(float v);
		static value vector(float x, float y, float z);
		static value of(std::uint32_t type, std::uint32_t word) { return { { word, 0, 0 }, type }; }

		bool is_real() const { return type == value_type::real; }
		float as_float() const;   // integers convert
		std::int32_t as_int() const; // floats truncate, like the exe's float-to-int helper
		float component(int i) const;
		std::uint32_t word() const { return words[0]; }
	};

	// Flags a statement leaves in the first byte of its value, read by the statement loop.
	namespace flow
	{
		constexpr std::uint8_t stop = 0x01;     // stop the scripts for this frame (a meta-action holds on)
		constexpr std::uint8_t wait = 0x02;     // leave the block; the schedule resumes here
		constexpr std::uint8_t done = 0x04;     // a meta-action finished
		constexpr std::uint8_t restart = 0x08;  // the behaviour changed: run the new one's rules
		constexpr std::uint8_t end = 0x10;      // the script reached its end
		constexpr std::uint8_t no_signal = 0x20;
	}

	class interpreter;

	// What a condition, function or procedure handler gets: the interpreter, the super object of the
	// actor whose script runs, and the node after its own (its arguments start there).
	struct call
	{
		interpreter& vm;
		std::uint32_t self; // super object
		std::uint32_t args;
		std::uint32_t index; // which entry of its table, for handlers shared by several entries
	};

	// Handlers return the node after their arguments.
	using condition_handler = std::function<std::uint32_t(call&, bool& result)>;
	using function_handler = std::function<std::uint32_t(call&, value& result)>;
	using procedure_handler = std::function<std::uint32_t(call&)>;
	// Meta-actions start once (reading their arguments into the 0x60 bytes of scratch memory at
	// `scratch`), then are checked every frame until done. The check returns flow flags: 0 when
	// finished, flow::wait to hold the schedule here.
	struct meta_handler
	{
		std::function<std::uint32_t(call&, std::uint32_t scratch)> start;
		std::function<std::uint8_t(interpreter&, std::uint32_t self, std::uint32_t scratch)> check;
	};

	// Runs the actors' scripts the way Rayman2.exe does: a tree-walking interpreter over the script
	// nodes in live memory, the behaviour runner (reflex, then rules and schedule), macros and
	// meta-actions that span several frames. Addresses in the comments are the exe's.
	class interpreter
	{
	public:
		// `main_actor` is the super object of Rayman (the game's main character).
		interpreter(live_memory& memory, std::uint32_t main_actor);

		// One frame of an actor's brain (0x4120D0): its reflex first, then, unless the reflex took
		// over, its normal behaviour.
		void think(std::uint32_t super_object);

		// When false (the default), every actor thinks every frame. The game lets scripts slow an
		// actor's brain to every Nth frame to save time on 1999 computers.
		bool honour_brain_period = false;

		void on_condition(std::uint32_t index, condition_handler h) { m_conditions[index] = std::move(h); }
		void on_function(std::uint32_t index, function_handler h) { m_functions[index] = std::move(h); }
		void on_procedure(std::uint32_t index, procedure_handler h) { m_procedures[index] = std::move(h); }
		void on_meta_action(std::uint32_t index, meta_handler h) { m_metas[index] = std::move(h); }

		// For handlers.
		live_memory& memory() { return m_memory; }
		std::uint32_t main_actor() const { return m_main_actor; }
		// The game's clock in milliseconds (0x5003EC), which timers in scripts compare against.
		std::uint32_t clock() const { return m_clock; }
		// Call once per tick (1/60 s), before the actors think.
		void next_tick()
		{
			std::uint32_t before = m_clock;
			m_ticks++;
			m_clock = std::uint32_t(m_ticks * 1000 / 60);
			m_frame_ms = m_clock - before;
		}
		std::uint32_t frame_ms() const { return m_frame_ms; }
		// The particles the scripts started (procedure 503).
		game::particles& particles() { return m_particles; }
		const game::particles& particles() const { return m_particles; }
		// What actors stand on and bump into.
		void set_world(world_collision world) { m_world = std::move(world); }
		const world_collision& world() const { return m_world; }
		// Sets a designer variable of an actor from outside the scripts (state carried over from
		// an earlier level, tests).
		void set_variable(std::uint32_t self, std::uint32_t index, const value& v) { write_variable(variable(self, index), v); }
		value get_variable(std::uint32_t self, std::uint32_t index) const { return read_variable(variable(self, index)); }
		std::uint32_t eval(std::uint32_t self, std::uint32_t node, value& out); // 0x480B90
		// Skips a node and its arguments.
		std::uint32_t skip(std::uint32_t node) const;
		// Writes a value to what the node names (a variable, a field, an actor's variable), as an
		// assignment would. Returns the node after it.
		std::uint32_t assign(std::uint32_t self, std::uint32_t node, const value& v);
		// The actor a procedure acts on: normally `self`, or the one an "actor: ..." operator named.
		std::uint32_t target(std::uint32_t self, bool allow_override);
		// Procedures report a change of flow (e.g. flow::restart after changing behaviour).
		void signal(std::uint8_t flags) { m_procedure_flags = flags; }
		// Procedure 88: the flags the next statement's result is merged with (0x4E9399, 0x4E9398).
		void set_pending_signal(std::uint8_t flags) { m_signal = flags; m_pending = true; }
		// Actors taken out of the world (procedure 139): they stop thinking and aren't drawn.
		void remove(std::uint32_t super_object) { m_removed.insert(super_object); }
		bool removed(std::uint32_t super_object) const { return m_removed.contains(super_object); }
		// The actor ("perso") of a super object, and back.
		std::uint32_t actor_of(std::uint32_t super_object) const;
		std::uint32_t super_object_of(std::uint32_t actor) const;
		// Switches an intelligence to a behaviour (0x469600). `other` when the switch comes from the
		// other intelligence (a reflex changing the normal behaviour), which doesn't restart this one.
		void change_behaviour(std::uint32_t intelligence, std::uint32_t behaviour, bool other);
		// True while the normal intelligence (not the reflex) runs.
		bool running_normal(std::uint32_t self) const;

		// The node evaluated last, to locate errors.
		std::uint32_t last_node() const { return m_last_node; }

		// The exe's movement input globals (0x4B9B68..0x4B9B98). Procedure 346 fills the first part
		// from the direction actions, and scripts read and write it as fields 11..17.
		struct movement_input
		{
			std::array<float, 3> vector{};          // 0x4B9B68, field 11
			std::int16_t short_a = 0, short_b = 0;  // 0x4B9B74, 0x4B9B76: fields 12, 13
			float real_a = 0, real_b = 0, real_c = 0; // 0x4B9B78..0x4B9B80: fields 14..16
			std::int32_t integer = 0;               // 0x4B9B84, field 17
			std::array<std::uint32_t, 5> actions{}; // 0x4B9B88..: up, down, left, right, walk (procedure 435)
			bool snap = false;                      // 0x4B9B9C: skip the next smoothing (procedure 347)
			float axis_h = 0, axis_v = 0;           // 0x4B9BA0, 0x4B9BA4: last smoothed key axes
		};
		movement_input movement;

		// The camera scripts' global tuning: the exe's settings block 0x4FF62C..0x4FF6DF (`values`,
		// by offset from its start) and the copy procedures 492 and 493 save and restore
		// (0x4FF6E0..), then the gauge pair functions 102..107 work on (0x4E9308, 0x4E930C). Their
		// initial values are still to be read from the exe, so they start at zero.
		struct camera_globals
		{
			std::array<std::uint8_t, 0xB4> values{};
			std::array<std::uint8_t, 0xB4> saved{};
			float gauge = 0;
			float gauge_max = 0;
		};
		camera_globals& camera() { return m_camera; }

		// The game camera: what its scripts' last solve asked for, and what the mechanics that moves
		// its super object carries from tick to tick (procedure 444, 0x4359D0; see camera.h).
		camera_state& camera_motion() { return m_camera_motion; }

		// Handlers that don't exist yet, by kind and index, and how often scripts reached them.
		// The key is the kind's name (a string literal) and the index.
		std::map<std::pair<const char*, std::uint32_t>, std::size_t> missing;
		// The same, by the super object whose scripts reached them.
		std::map<std::uint32_t, std::map<std::pair<const char*, std::uint32_t>, std::size_t>> missing_by_actor;

	private:
		std::uint32_t m_running = 0; // the super object whose scripts run now
		struct node_view
		{
			std::uint32_t param;
			std::uint16_t cache;
			std::uint8_t indent, type;
		};
		node_view node(std::uint32_t address) const;

		std::uint32_t block(std::uint32_t self, std::uint32_t node, value& out);           // 0x480BD0
		std::uint8_t settle(const value& out, std::uint32_t next, std::uint8_t indent);       // 0x480D10
		bool run(std::uint32_t self, std::uint32_t intelligence);                              // 0x480D80
		std::uint8_t run_rules(std::uint32_t self, std::uint32_t intelligence);                // 0x480E00
		std::uint8_t run_schedule(std::uint32_t self, std::uint32_t intelligence);             // 0x480ED0
		void reset_schedule(std::uint32_t intelligence);                                       // 0x480F70

		std::uint32_t keyword(std::uint32_t self, std::uint32_t node, value& out);
		std::uint32_t operation(std::uint32_t self, std::uint32_t node, value& out);
		std::uint32_t assignment(std::uint32_t self, std::uint32_t node, std::uint32_t op, value& out);
		std::uint32_t subroutine(std::uint32_t self, std::uint32_t node, value& out);       // 0x480220
		std::uint32_t meta_action(std::uint32_t self, std::uint32_t node, value& out);      // 0x480A50

		// Designer variables: where variable `index` of an actor lives, and its type.
		struct variable_slot
		{
			std::uint32_t address = 0;
			std::uint32_t type = 0;
		};
		variable_slot variable(std::uint32_t self, std::uint32_t index) const;               // 0x4815F0
		value read_variable(const variable_slot& slot) const;
		void write_variable(const variable_slot& slot, const value& v);

		// Something an assignment can write to.
		struct place
		{
			enum class kind { variable, field, component, element } what = kind::variable;
			variable_slot slot;
			std::uint32_t actor = 0; // fields: whose
			std::uint32_t field = 0;
			int component = 0;      // vectors: 0..2
			std::uint32_t element = 0; // arrays
		};
		std::uint32_t resolve_place(std::uint32_t self, std::uint32_t node, place& p);
		value read_place(std::uint32_t, const place& p);
		void write_place(std::uint32_t self, const place& p, const value& v);

		// Multi-frame actions in progress, kept per intelligence (+0x10): 0x68-byte entries.
		std::uint8_t find_action(std::uint32_t intelligence, std::uint8_t script) const;                      // 0x4826A0
		std::uint8_t find_action(std::uint32_t intelligence, std::uint8_t script, std::uint32_t key) const;   // 0x4826E0
		std::uint8_t free_action(std::uint32_t intelligence) const;                                            // 0x482730
		void push_action(std::uint32_t intelligence, std::uint8_t script, std::uint32_t node);                // 0x482760
		void pop_action(std::uint32_t intelligence);                                                           // 0x482820
		void drop_actions_after(std::uint32_t intelligence, std::uint8_t script);                             // 0x482870
		void drop_schedule_actions(std::uint32_t intelligence);                                                // 0x4828E0
		void clear_actions(std::uint32_t intelligence);                                                        // 0x482940
		std::uint32_t resume_action(std::uint32_t intelligence, std::uint8_t entry);                          // 0x482990
		void write_action(std::uint32_t intelligence, std::uint8_t entry, std::uint8_t script, std::uint32_t node, bool used); // 0x4827B0

		void note_missing(const char* kind, std::uint32_t index);

		live_memory& m_memory;
		std::uint32_t m_main_actor = 0;
		std::map<std::uint32_t, condition_handler> m_conditions;
		std::map<std::uint32_t, function_handler> m_functions;
		std::map<std::uint32_t, procedure_handler> m_procedures;
		std::map<std::uint32_t, meta_handler> m_metas;

		// The exe's globals for the running script.
		std::uint8_t m_frame = 0;            // 0x4A7070: the actor's frame counter, for every-N-frames ifs
		std::uint8_t m_signal = flow::no_signal; // 0x4E9399
		bool m_pending = false;              // 0x4E9398
		bool m_acted = false;                // 0x4E939B: a meta-action ran
		std::uint8_t m_procedure_flags = 0;  // 0x4B9C24
		bool m_schedule = false;             // 0x4B9C18: running the schedule (not rules)
		std::uint8_t m_script = 0;           // 0x4E939A: 0 = schedule, n = rule n
		std::uint32_t m_intelligence = 0;    // 0x4E939C
		std::uint8_t m_macro_depth = 0;      // 0x4B9C1C
		std::uint32_t m_macro_node = 0;      // 0x4B9C20
		std::uint32_t m_acting = 0;          // 0x4B9C28: "actor: ..." override
		std::uint32_t m_last_node = 0;
		std::uint32_t m_clock = 0;
		std::set<std::uint32_t> m_removed;
		std::uint64_t m_ticks = 0;
		std::uint32_t m_frame_ms = 16;
		game::particles m_particles;
		world_collision m_world;
		camera_globals m_camera;
		camera_state m_camera_motion;
	};
}
